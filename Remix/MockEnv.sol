// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "./ArbitrageBot.sol";

contract MockERC20 is IERC20 {
    mapping(address => uint256) public override balanceOf;
    mapping(address => mapping(address => uint256)) public allowances;

    function mint(address to, uint amount) external {
        balanceOf[to] += amount;
    }

    function transfer(address to, uint amount) external override returns (bool) {
        balanceOf[msg.sender] -= amount;
        balanceOf[to] += amount;
        return true;
    }

    function transferFrom(address from, address to, uint amount) external override returns (bool) {
        balanceOf[from] -= amount;
        balanceOf[to] += amount;
        return true;
    }

    function approve(address spender, uint amount) external override returns (bool) {
        allowances[msg.sender][spender] = amount;
        return true;
    }
}

contract MockRouter is IRouter {
    mapping(bytes32 => uint256[2]) public reserves;

    function setReserve(address tokenA, address tokenB, uint256 rA, uint256 rB) external {
        reserves[keccak256(abi.encodePacked(tokenA, tokenB))] = [rA, rB];
        reserves[keccak256(abi.encodePacked(tokenB, tokenA))] = [rB, rA];
    }

    function getAmountsOut(uint amountIn, address[] calldata path) external view override returns (uint[] memory amounts) {
        amounts = new uint[](path.length);
        amounts[0] = amountIn;
        for (uint i = 0; i < path.length - 1; i++) {
            bytes32 key = keccak256(abi.encodePacked(path[i], path[i+1]));
            uint256 rIn = reserves[key][0];
            uint256 rOut = reserves[key][1];
            require(rIn > 0, "Reserve not set");
            
            uint256 amountInWithFee = amounts[i] * 997;
            amounts[i+1] = (amountInWithFee * rOut) / (rIn * 1000 + amountInWithFee);
        }
    }

    function swapExactTokensForTokens(
        uint amountIn, uint, address[] calldata path, address to, uint
    ) external override returns (uint[] memory amounts) {
        amounts = this.getAmountsOut(amountIn, path);
        // Simulate transfer to recipient
        MockERC20(path[path.length - 1]).mint(to, amounts[amounts.length - 1]);
    }
}
