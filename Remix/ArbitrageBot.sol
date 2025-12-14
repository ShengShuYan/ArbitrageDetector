// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

interface IERC20 {
    function transferFrom(address sender, address recipient, uint256 amount) external returns (bool);
    function transfer(address recipient, uint256 amount) external returns (bool);
    function balanceOf(address account) external view returns (uint256);
    function approve(address spender, uint256 amount) external returns (bool);
}

interface IRouter {
    function getAmountsOut(uint amountIn, address[] calldata path) external view returns (uint[] memory amounts);
    function swapExactTokensForTokens(
        uint amountIn, uint amountOutMin, address[] calldata path, address to, uint deadline
    ) external returns (uint[] memory amounts);
}

contract ArbitrageBot {
    address public owner;
    IRouter public router;

    struct CycleData {
        address[] path;
        uint256 amountIn;
        uint256 minProfit;
    }

    constructor(address _router) {
        owner = msg.sender;
        router = IRouter(_router);
    }

    modifier onlyOwner() {
        require(msg.sender == owner, "Auth failed");
        _;
    }

    function executeArbitrage(CycleData calldata params) external onlyOwner {
        uint256 startBal = IERC20(params.path[0]).balanceOf(msg.sender);
        require(startBal >= params.amountIn, "Insufficient balance");

        require(IERC20(params.path[0]).transferFrom(msg.sender, address(this), params.amountIn), "Transfer failed");
        IERC20(params.path[0]).approve(address(router), params.amountIn);

        // Validation: Re-fetch reserves & Check profitability
        uint256[] memory amounts = router.getAmountsOut(params.amountIn, params.path);
        uint256 expectedOut = amounts[amounts.length - 1];
        require(expectedOut >= params.amountIn + params.minProfit, "Validation failed: Unprofitable");

        // Execution: Atomic Swap
        router.swapExactTokensForTokens(
            params.amountIn, 0, params.path, address(this), block.timestamp
        );

        // Settlement: Revert on Loss
        uint256 finalBal = IERC20(params.path[0]).balanceOf(address(this));
        require(finalBal > params.amountIn, "Execution failed: Loss detected");

        IERC20(params.path[0]).transfer(msg.sender, finalBal);
    }
}
