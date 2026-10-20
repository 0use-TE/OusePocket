$ErrorActionPreference='Stop'
$taskCli='D:/Software/lceda-pro/lceda-pro.exe'
$taskSession='89b27b1d-7de8-432e-8648-c9b901bcea83'
$taskOut=$PSScriptRoot
function Export-EdaText($taskCode,$taskName){
    $taskResult=& $taskCli invoke --session $taskSession --timeout 120000 --code $taskCode | Out-String | ConvertFrom-Json
    if(-not $taskResult.ok){throw ($taskResult|ConvertTo-Json -Depth 4)}
    $taskLength=$taskResult.value.chars
    $taskBuilder=[Text.StringBuilder]::new()
    for($taskOffset=0;$taskOffset -lt $taskLength;$taskOffset+=2000000){
        $taskSlice=& $taskCli invoke --session $taskSession --code 'return globalThis.buttonExport.slice(__CLI__.args.start,__CLI__.args.start+2000000);' --args ('{"start":'+$taskOffset+'}') | Out-String | ConvertFrom-Json
        if(-not $taskSlice.ok){throw 'Chunk export failed'}
        [void]$taskBuilder.Append($taskSlice.value)
    }
    if($taskName.EndsWith('.b64')){
        $taskTarget=Join-Path $taskOut $taskName.Substring(0,$taskName.Length-4)
        [IO.File]::WriteAllBytes($taskTarget,[Convert]::FromBase64String($taskBuilder.ToString()))
    }else{
        [IO.File]::WriteAllText((Join-Path $taskOut $taskName),$taskBuilder.ToString(),[Text.UTF8Encoding]::new($false))
    }
    Write-Output ($taskName+': '+$taskLength+' chars')
}
Export-EdaText 'const f=await eda.pcb_ManufactureData.get3DFile("button-led-v1","step",["Component Model"],"Parts",true);globalThis.buttonExport=await f.text();return {chars:globalThis.buttonExport.length};' 'button-led-v1.step'
Export-EdaText 'const f=await eda.sys_FileManager.getProjectFile("button-led-v1",undefined,"epro2");const a=new Uint8Array(await f.arrayBuffer());let s="";for(let i=0;i<a.length;i+=32768)s+=String.fromCharCode(...a.subarray(i,i+32768));globalThis.buttonExport=btoa(s);return {chars:globalThis.buttonExport.length};' 'button-led-v1.epro2.b64'
Export-EdaText 'const f=await eda.pcb_ManufactureData.getGerberFile("button-led-v1");const a=new Uint8Array(await f.arrayBuffer());let s="";for(let i=0;i<a.length;i+=32768)s+=String.fromCharCode(...a.subarray(i,i+32768));globalThis.buttonExport=btoa(s);return {chars:globalThis.buttonExport.length};' 'button-led-v1-gerber.zip.b64'
