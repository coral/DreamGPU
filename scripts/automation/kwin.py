"""Bounded KWin observation for the owned Wayland Juke window.

Uses KWin's documented PID/minimized properties and callDBus, not screenshots
or journal parsing: https://develop.kde.org/docs/plasma/kwin/api/
Only restore=True mutates a window, and only after a unique exact PID match.
A uniquely named temporary script is unloaded in finally; no KWin config changes.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import asyncio
import json
from pathlib import Path
import tempfile
import uuid


def validate(value,pid,token):
    if not isinstance(value,dict) or value.get('token')!=token or value.get('pid')!=pid:
        raise ValueError('KWin observer identity mismatch')
    if value.get('count')!=1 or type(value.get('minimized')) is not bool:
        raise ValueError('KWin requires exactly one owned Juke window with observable state')
    return {'minimized':value['minimized'],'focused':value.get('focused'),
            'observer':'KWin compositor','pid':pid,'window':value.get('window')}


async def observe_async(pid,restore=False):
    from dbus_next import Message,MessageType
    from dbus_next.aio import MessageBus
    from dbus_next.service import ServiceInterface,method
    if type(pid) is not int or pid<=0:raise ValueError('An exact owned PID is required')
    token=uuid.uuid4().hex;service='org.juke.WindowProbe.x'+token;plugin='juke-window-probe-'+token
    done=asyncio.Event();result=[];bus=None;loaded=False
    class Receiver(ServiceInterface):
        def __init__(self):super().__init__('org.juke.WindowProbe')
        @method()
        def Record(self,payload:'s'):
            if len(payload)>1024 or result:return
            result.append(json.loads(payload));done.set()
    async def call(path,interface,member,signature='',body=None):
        reply=await asyncio.wait_for(bus.call(Message(destination='org.kde.KWin',path=path,
            interface=interface,member=member,signature=signature,body=body or [])),2)
        if reply.message_type==MessageType.ERROR:raise RuntimeError('KWin '+member+': '+str(reply.body))
        return reply.body
    with tempfile.TemporaryDirectory(prefix='juke-kwin-') as folder:
        script=Path(folder)/'observe.js'
        script.write_text('''(function(){
var matches=workspace.windowList().filter(function(w){return w.pid===PID;});
var result={token:TOKEN,pid:PID,count:matches.length};
if(matches.length===1){var w=matches[0];
 if(RESTORE){w.minimized=false;}
 result.minimized=w.minimized;result.focused=w.active;result.window=String(w.internalId);
}
callDBus(SERVICE,"/Result","org.juke.WindowProbe","Record",JSON.stringify(result));
})();
'''.replace('PID',str(pid)).replace('TOKEN',json.dumps(token)).replace('RESTORE','true' if restore else 'false').replace('SERVICE',json.dumps(service)))
        script.chmod(0o600)
        try:
            bus=await asyncio.wait_for(MessageBus().connect(),2)
            bus.export('/Result',Receiver());await asyncio.wait_for(bus.request_name(service),2)
            number=(await call('/Scripting','org.kde.kwin.Scripting','loadScript','ss',[str(script),plugin]))[0]
            if type(number) is not int or number<0:raise RuntimeError('KWin refused the temporary observer')
            loaded=True
            await call('/Scripting/Script'+str(number),'org.kde.kwin.Script','run')
            await asyncio.wait_for(done.wait(),2)
            return validate(result[0],pid,token)
        finally:
            try:
                if loaded:
                    removed=await call('/Scripting','org.kde.kwin.Scripting','unloadScript','s',[plugin])
                    if removed!=[True]:raise RuntimeError('KWin temporary observer did not unload')
            finally:
                if bus:bus.disconnect()


def observe(pid,restore=False):
    async def bounded():
        return await asyncio.wait_for(observe_async(pid,restore),2)
    return asyncio.run(bounded())
