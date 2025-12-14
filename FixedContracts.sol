// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract MockERC20 {
    string public name = "Mock Token";
    string public symbol = "MOCK";
    uint8 public decimals = 18;
    uint256 public totalSupply;
    mapping(address => uint256) public balanceOf;
    mapping(address => mapping(address => uint256)) public allowance;

    function mint(address to, uint256 amount) external {
        balanceOf[to] += amount;
        totalSupply += amount;
    }

    function transfer(address to, uint256 amount) external returns (bool) {
        return _transfer(msg.sender, to, amount);
    }

    function transferFrom(address from, address to, uint256 amount) external returns (bool) {
        require(allowance[from][msg.sender] >= amount, "Allowance exceeded");
        allowance[from][msg.sender] -= amount;
        return _transfer(from, to, amount);
    }

    function approve(address spender, uint256 amount) external returns (bool) {
        allowance[msg.sender][spender] = amount;
        return true;
    }

    function _transfer(address from, address to, uint256 amount) internal returns (bool) {
        require(balanceOf[from] >= amount, "Insufficient balance");
        balanceOf[from] -= amount;
        balanceOf[to] += amount;
        return true;
    }
}

interface IRouter {
    function swapExactTokensForTokens(
        uint amountIn,
        uint amountOutMin,
        address[] calldata path,
        address to,
        uint deadline
    ) external returns (uint[] memory amounts);
}

contract MockRouter is IRouter {
    struct Pool {
        uint256 reserve0;
        uint256 reserve1;
    }
    mapping(bytes32 => Pool) public pools;

    function _key(address a, address b) internal pure returns (bytes32) {
        return keccak256(abi.encodePacked(a, b));
    }

    function setReserve(address tokenA, address tokenB, uint256 rA, uint256 rB) external {
        pools[_key(tokenA, tokenB)] = Pool(rA, rB);
        pools[_key(tokenB, tokenA)] = Pool(rB, rA);
    }

    // Stateful Swap: Updates reserves explicitly
    function swapExactTokensForTokens(
        uint amountIn,
        uint amountOutMin,
        address[] calldata path,
        address to,
        uint // deadline (Unused parameter removed)
    ) external override returns (uint[] memory amounts) {
        amounts = new uint[](path.length);
        amounts[0] = amountIn;

        for (uint i = 0; i < path.length - 1; i++) {
            address tokenIn = path[i];
            address tokenOut = path[i+1];
            bytes32 key = _key(tokenIn, tokenOut);

            Pool storage p = pools[key];
            require(p.reserve0 > 0 && p.reserve1 > 0, "Pool not set");

            uint amountInWithFee = amounts[i] * 997;
            uint numerator = amountInWithFee * p.reserve1;
            uint denominator = (p.reserve0 * 1000) + amountInWithFee;
            uint amountOut = numerator / denominator;

            amounts[i+1] = amountOut;

            // UPDATE STATE
            p.reserve0 += amounts[i];
            p.reserve1 -= amountOut;

            Pool storage pRev = pools[_key(tokenOut, tokenIn)];
            pRev.reserve0 = p.reserve1;
            pRev.reserve1 = p.reserve0;
        }

        require(amounts[amounts.length - 1] >= amountOutMin, "Slippage too high");
        MockERC20(path[path.length - 1]).mint(to, amounts[amounts.length - 1]);
    }
}

contract ArbitrageBot {
    address public router;
    address public owner;

    constructor(address _router) {
        router = _router;
        owner = msg.sender;
    }

    struct CycleInput {
        address[] path;
        uint256 amountIn;
        uint256 minProfit; 
    }

    function executeArbitrage(CycleInput calldata params) external {
        address startToken = params.path[0];
        MockERC20(startToken).transferFrom(msg.sender, address(this), params.amountIn);
        MockERC20(startToken).approve(router, params.amountIn);

        // Call router without storing unused return variable
        IRouter(router).swapExactTokensForTokens(
            params.amountIn,
            params.minProfit,
            params.path,
            address(this),
            block.timestamp
        );

        uint finalBalance = MockERC20(startToken).balanceOf(address(this));
        require(finalBalance > params.amountIn, "No profit");
        MockERC20(startToken).transfer(msg.sender, finalBalance);
    }
}