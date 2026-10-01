const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('firmware/RetroSports/Portal.h','utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
async function scenario(statuses,elapsed=0){
 const nodes={},timers=[];let now=0,index=0;
 const context={document:{getElementById(id){return nodes[id]??=( {hidden:true,textContent:'',disabled:false});}},Date:{now:()=>now},AbortSignal:{timeout:()=>({})},setTimeout:fn=>timers.push(fn),fetch:async()=>{const value=statuses[Math.min(index++,statuses.length-1)];if(value instanceof Error)throw value;return{ok:true,json:async()=>value};}};
 vm.createContext(context);vm.runInContext(script,context);await new Promise(setImmediate);
 now=elapsed;
 return{nodes,timers,async next(){const fn=timers.shift();if(fn)await fn();await new Promise(setImmediate);}};
}
(async()=>{
 let x=await scenario([{state:'connecting'},{state:'connected',clockReady:true}]);assert(x.timers.length);await x.next();assert.equal(x.nodes.title.textContent,'Connected!');assert.equal(x.nodes.success.hidden,false);assert.equal(x.nodes.retry.hidden,true);
 x=await scenario([{state:'failed',message:'Check your password'}]);assert.equal(x.nodes.title.textContent,'Could not connect');assert.equal(x.nodes.message.textContent,'Check your password');assert.equal(x.nodes.retry.hidden,false);
 x=await scenario([new Error('lost hotspot')],46000);await x.next();assert.equal(x.nodes.title.textContent,'Check the scoreboard');assert.equal(x.nodes.retry.hidden,false);
 x=await scenario([{state:'connected',clockReady:false}]);assert.equal(x.nodes.title.textContent,'Connected!');assert(x.nodes.message.textContent.includes('setting its clock'));
 console.log('PASS: portal success, failure, hotspot interruption timeout, and connected-before-clock states');
})();
