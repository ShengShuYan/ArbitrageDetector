(async () => {
    try {
        console.log(">>> STARTING FORENSIC AUDIT SIMULATION <<<");
        const accounts = await web3.eth.getAccounts();
        const deployer = accounts[0]; 

        // 1. 辅助函数：大数处理
        function toBigIntSmart(valueStr, decimals) {
            if (!valueStr) return "0";
            const strVal = String(valueStr);
            if (strVal.includes('.')) {
                const dec = parseInt(decimals || 18);
                let [integer, fraction] = strVal.split('.');
                fraction = fraction || "";
                if (fraction.length > dec) fraction = fraction.slice(0, dec);
                while (fraction.length < dec) fraction += "0";
                return BigInt(integer + fraction).toString();
            }
            return BigInt(strVal).toString();
        }

        function fromWei(weiStr) {
            return web3.utils.fromWei(String(weiStr), 'ether');
        }

        // 2. 加载数据
        console.log("Loading files...");
        const poolsContent = await remix.call('fileManager', 'getFile', 'v2pools.json');
        const allPools = JSON.parse(poolsContent);
        const oppContent = await remix.call('fileManager', 'getFile', 'weth_opportunities.json');
        const opportunities = JSON.parse(oppContent); 
        console.log(`Loaded ${opportunities.length} cycles to audit.`);

        // 3. 部署合约
        const routerMeta = JSON.parse(await remix.call('fileManager', 'getFile', 'browser/artifacts/MockRouter.json'));
        const botMeta = JSON.parse(await remix.call('fileManager', 'getFile', 'browser/artifacts/ArbitrageBot.json'));
        const tokenMeta = JSON.parse(await remix.call('fileManager', 'getFile', 'browser/artifacts/MockERC20.json'));

        let router = new web3.eth.Contract(routerMeta.abi);
        router = await router.deploy({ data: routerMeta.data.bytecode.object }).send({ from: deployer });
        const routerAddr = router.options.address;

        let bot = new web3.eth.Contract(botMeta.abi);
        bot = await bot.deploy({ data: botMeta.data.bytecode.object, arguments: [routerAddr] }).send({ from: deployer });
        const botAddr = bot.options.address;

        let addressMap = {}; 
        let totalGasEth = 0.0;
        let totalProfitEth = 0.0;

        console.log("\n---------------------------------------------------");

        for (let i = 0; i < opportunities.length; i++) {
            const opp = opportunities[i];
            const realPath = opp.path;
            
            try {
                // A. 部署/映射代币
                for (let realAddr of realPath) {
                    if (!addressMap[realAddr]) {
                        let mock = new web3.eth.Contract(tokenMeta.abi);
                        mock = await mock.deploy({ data: tokenMeta.data.bytecode.object }).send({ from: deployer });
                        addressMap[realAddr] = mock.options.address;
                    }
                }

                // B. 设置池子深度 (Set Reserves)
                for (let k = 0; k < realPath.length - 1; k++) {
                    const realA = realPath[k];
                    const realB = realPath[k+1];
                    // 查找池子数据...
                    const poolData = allPools.find(p => 
                        (p.token0.id === realA && p.token1.id === realB) ||
                        (p.token0.id === realB && p.token1.id === realA)
                    );

                    if (poolData) {
                        const isOrder0 = (poolData.token0.id === realA);
                        const rA_raw = isOrder0 ? poolData.reserve0 : poolData.reserve1;
                        const rB_raw = isOrder0 ? poolData.reserve1 : poolData.reserve0;
                        const decA = isOrder0 ? (poolData.token0.decimals || 18) : (poolData.token1.decimals || 18);
                        const decB = isOrder0 ? (poolData.token1.decimals || 18) : (poolData.token0.decimals || 18);
                        
                        await router.methods.setReserve(
                            addressMap[realA], addressMap[realB], 
                            toBigIntSmart(rA_raw, decA), 
                            toBigIntSmart(rB_raw, decB)
                        ).send({ from: deployer });
                    }
                }

                // C. 准备资金
                const mockPath = realPath.map(addr => addressMap[addr]);
                const startToken = new web3.eth.Contract(tokenMeta.abi, mockPath[0]);
                const inputWei = toBigIntSmart(opp.inputAmount, 18); // WETH 18 decimals

                await startToken.methods.mint(deployer, inputWei).send({ from: deployer });
                await startToken.methods.approve(botAddr, inputWei).send({ from: deployer });

                // D. 执行并抓取 GAS (The Forensic Part)
                const balBefore = await startToken.methods.balanceOf(deployer).call();

                // 使用 send() 会返回 receipt
                const receipt = await bot.methods.executeArbitrage({
                    path: mockPath,
                    amountIn: inputWei,
                    minProfit: inputWei // 保本模式
                }).send({ from: deployer });

                const balAfter = await startToken.methods.balanceOf(deployer).call();
                
                // --- 计算真实数据 ---
                const rawProfitWei = BigInt(balAfter) - BigInt(balBefore);
                const gasUsed = receipt.gasUsed;
                // 假设 Gas Price 32 Gwei (0.000000032 ETH)
                const gasCostEth = parseFloat(gasUsed) * 0.000000032;
                const rawProfitEth = parseFloat(fromWei(rawProfitWei));
                const netProfitEth = rawProfitEth - gasCostEth;

                console.log(`Cycle #${i + 1}: ON-CHAIN SUCCESS`);
                console.log(`   Input:       ${opp.inputAmount} ETH`);
                console.log(`   Gross Gain:  ${rawProfitEth.toFixed(6)} ETH (Calculated by Solidity)`);
                console.log(`   Gas Used:    ${gasUsed} units`);
                console.log(`   Gas Cost:    -${gasCostEth.toFixed(6)} ETH (@ 32 Gwei)`);
                
                if (netProfitEth > 0) {
                    console.log(` NET PROFIT: ${netProfitEth.toFixed(6)} ETH`);
                    totalProfitEth += netProfitEth;
                } else {
                    console.log(` NET LOSS:   ${netProfitEth.toFixed(6)} ETH (Gas ate profit)`);
                }
                console.log("---------------------------------------------------");

            } catch (e) {
                // 如果是 Revert，通常是因为滑点太高导致 Output < Input
                // console.log(`Cycle #${i + 1}: Reverted (Slippage/Loss detected on-chain)`);
            }
        }
        
        console.log(`\n>>> AUDIT COMPLETE <<<`);
        console.log(`Total Verified Net Profit: ${totalProfitEth.toFixed(4)} ETH`);

    } catch (e) {
        console.error("Script Error:", e.message);
    }
})()