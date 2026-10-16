"""Local stdio MCP bridge to a wired Ouse board; stdout is JSON-RPC only."""
import argparse
import base64
import json
import sys
from pathlib import Path
from device import Device


def tool(name,description,properties=None,required=None):
    return dict(name=name,description=description,inputSchema=dict(type="object",properties=properties or {},required=required or [],additionalProperties=False))


integer=dict(type="integer",minimum=0,maximum=4095)
TOOLS=[
    tool("device_state","Read actual device UI and app state."),
    tool("device_tree","Read visible UI elements, labels and screen coordinates."),
    tool("device_screenshot","Capture the current LVGL screen as a JPEG image."),
    tool("device_tap","Tap through the real LVGL touch/gesture input path.",dict(x=integer,y=integer),["x","y"]),
    tool("device_swipe","Swipe; hold_ms pauses with the finger down to enter overview.",dict(x=integer,y=integer,end_x=integer,end_y=integer,duration_ms=dict(type="integer",minimum=80,maximum=3000),hold_ms=dict(type="integer",minimum=0,maximum=5000)),["x","y","end_x","end_y"]),
    tool("device_open_app","Open an installed or built-in app.",dict(id=dict(type="string",maxLength=47)),["id"]),
    tool("device_home","Return to the desktop."),
    tool("device_test","Run firmware Unity tests on the actual board."),
    tool("device_stress","Repeatedly open apps and exercise home/overview gestures; saves UART/core dumps.",dict(rounds=dict(type="integer",minimum=1,maximum=200))),
]


def execute(device,name,args):
    if name=="device_screenshot":
        path=device.screenshot()
        return dict(content=[dict(type="image",mimeType="image/jpeg",data=base64.b64encode(path.read_bytes()).decode())])
    if name=="device_state":result=device.state()
    elif name=="device_tree":result=device.tree()
    elif name=="device_tap":result=device.tap(**args)
    elif name=="device_swipe":result=device.swipe(**args)
    elif name=="device_open_app":device.request("DBG_OPEN",args["id"]);result=device.state()
    elif name=="device_home":device.request("DBG_HOME");result=device.state()
    elif name=="device_test":result=device.tests()
    elif name=="device_stress":result=device.stress(**args)
    else:raise ValueError("Unknown tool: "+name)
    return dict(content=[dict(type="text",text=json.dumps(result,ensure_ascii=False))],isError=name=="device_test" and bool(result["failures"]))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",default="COM10")
    parser.add_argument("--baud",type=int,default=921600)
    parser.add_argument("--elf")
    args=parser.parse_args()
    device=None
    try:
        for line in sys.stdin:
            message={}
            try:
                message=json.loads(line)
                if "id" not in message:continue
                method=message["method"]
                if method=="initialize":
                    result=dict(protocolVersion="2024-11-05",capabilities=dict(tools={}),serverInfo=dict(name="ouse-device",version=(Path(__file__).resolve().parents[1]/"version.txt").read_text(encoding="utf-8").strip()))
                elif method=="ping":result={}
                elif method=="tools/list":result=dict(tools=TOOLS)
                elif method=="tools/call":
                    if device is None:
                        device=Device(args.port,elf=args.elf,baud=args.baud)
                        if "debug=1" not in device.request("HELLO"):raise RuntimeError("Debug firmware required")
                    params=message["params"]
                    try:result=execute(device,params["name"],params.get("arguments",{}))
                    except Exception as error:result=dict(isError=True,content=[dict(type="text",text=str(error))])
                else:
                    print(json.dumps(dict(jsonrpc="2.0",id=message["id"],error=dict(code=-32601,message="Method not found"))),flush=True)
                    continue
                print(json.dumps(dict(jsonrpc="2.0",id=message["id"],result=result),ensure_ascii=False),flush=True)
            except Exception as error:
                print(json.dumps(dict(jsonrpc="2.0",id=message.get("id"),error=dict(code=-32603,message=str(error)))),flush=True)
    finally:
        if device:device.close()


if __name__=="__main__": main()
