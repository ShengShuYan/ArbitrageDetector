# ArbitrageDetector
Uniswap V2 Arbitrage Detection System

## Project Overview
This repository implements a arbitrage system designed for Uniswap V2, integrating off-chain calculation with trustless on-chain execution.

**Core Components:**
* **Off-Chain Strategy (C++):** Implements an optimized SPFA algorithm for negative cycle detection and Golden Section Search for input optimization.
* **On-Chain Validation (Solidity):** A custom atomic contract (`ArbitrageBot.sol`) enforcing "Revert-on-Loss" logic to guarantee zero principal risk.
* **Simulation (JavaScript):** A Web3.js-based backtesting script for verifying execution logic and gas profitability in a forked environment.

## Known Limitation: Profit Precision
Discrepancies may be observed between the off-chain predicted profit (C++) and the on-chain realized profit (JavaScript).
* **Root Cause:** Inconsistencies in token decimal alignment (e.g., normalizing 6-decimal USDC vs. 18-decimal WETH) during the graph construction phase.
* **Status:** This is a known issue scheduled for resolution in the next iteration.

## AI Usage Declaration
Portions of the overlap between different programming languages and implementation were assisted by AI tools. All logic and implementations have been manually reviewed and verified by the author.

