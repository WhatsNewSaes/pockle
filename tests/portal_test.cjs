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
 x=await scenario([new Error('lost hotspot')],46000);await x.next();assert.equal(x.nodes.title.textContent,'Check Pockle');assert.equal(x.nodes.retry.hidden,false);
 x=await scenario([{state:'connected',clockReady:false}]);assert.equal(x.nodes.title.textContent,'Connected!');assert(x.nodes.message.textContent.includes('setting its clock'));
 console.log('PASS: portal success, failure, hotspot interruption timeout, and connected-before-clock states');
 // The setup form's script: the phone's timezone picks the zone unless one is saved, the dropdown's pick hides the typed name,
 // an open one hides the password, and CONNECT is off until the password (and, for another network, its name) is typed.
 const setup=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1])[1];let n;
 function form(tz,options,saved='0',password='secret',ssid=''){
  const selectedIndex=Math.max(0,options.findIndex(o=>o.selected));
  const nodes={zone:{value:'0',dataset:{saved}},zonenote:{textContent:'Pick'},pick:{options,selectedIndex},other:{hidden:true},ssid:{required:false,value:ssid},pw:{hidden:false},password:{value:password,type:'password'},show:{checked:false},connect:{disabled:true},epoch:{value:''}};
  const context={document:{getElementById:id=>nodes[id]},Intl:{DateTimeFormat:()=>({resolvedOptions:()=>({timeZone:tz})})},Date:{now:()=>1791207114000}};
  vm.createContext(context);vm.runInContext(setup,context);return nodes;
 }
 const none={value:'',dataset:{none:'1'},selected:true},home={value:'Home',dataset:{open:'0'}},cafe={value:'Cafe',dataset:{open:'1'}},other={value:'',dataset:{open:'0'}};
 n=form('America/New_York',[none,home,cafe,other]);assert.equal(n.other.hidden,true);assert.equal(n.pw.hidden,false);assert.equal(n.connect.disabled,true); // nothing chosen yet: the password box shows, CONNECT stays off even with a password typed
 n.pick.selectedIndex=1;n.pick.onchange();assert.equal(n.connect.disabled,false);
 home.selected=true;none.selected=false;
 n=form('America/Chicago',[home,cafe,other]);assert.equal(n.zone.value,'1');assert.equal(n.zonenote.textContent,'Detected from your phone.');assert.equal(n.other.hidden,true);assert.equal(n.ssid.required,false);assert.equal(n.pw.hidden,false);assert.equal(n.epoch.value,1791207114);assert.equal(n.connect.disabled,false);
 n=form('America/Chicago',[home,cafe,other],'1');assert.equal(n.zone.value,'0');assert.equal(n.zonenote.textContent,'Pick');
 n=form('Europe/London',[home,cafe,other]);assert.equal(n.zone.value,'0');
 assert.equal(form('America/Indiana/Knox',[home]).zone.value,'1');assert.equal(form('America/Kentucky/Louisville',[home]).zone.value,'0');assert.equal(form('America/Los_Angeles',[home]).zone.value,'3');
 n=form('America/New_York',[home,cafe,other],'0','');assert.equal(n.connect.disabled,true);n.password.value='pw';n.password.oninput();assert.equal(n.connect.disabled,false); // typing the password turns CONNECT on
 n.pick.selectedIndex=1;n.pick.onchange();assert.equal(n.pw.hidden,true);assert.equal(n.password.value,'');assert.equal(n.connect.disabled,false); // an open network: no password, CONNECT on
 n.pick.selectedIndex=2;n.pick.onchange();assert.equal(n.other.hidden,false);assert.equal(n.ssid.required,true);assert.equal(n.pw.hidden,false);assert.equal(n.connect.disabled,true); // another network needs a name and a password
 n.ssid.value='Attic';n.ssid.oninput();assert.equal(n.connect.disabled,true);n.password.value='pw';n.password.oninput();assert.equal(n.connect.disabled,false);
 n.pick.selectedIndex=0;n.pick.onchange();assert.equal(n.other.hidden,true);n.show.checked=true;n.show.onchange();assert.equal(n.password.type,'text');
 n=form('America/New_York',[{...other,selected:true}]);assert.equal(n.other.hidden,false);assert.equal(n.ssid.required,true);assert.equal(n.connect.disabled,true); // no networks found: the typed name is shown and required
 console.log('PASS: setup form zone detection, saved zone, dropdown pick, open network, other network, CONNECT gating, and show password');
})();
