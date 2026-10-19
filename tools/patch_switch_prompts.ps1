param(
    [Parameter(Mandatory=$true)][string]$Assembly,
    [Parameter(Mandatory=$true)][string]$InputSystemAssembly,
    [Parameter(Mandatory=$true)][string]$Cecil
)
$ErrorActionPreference='Stop'
Add-Type -Path $Cecil
$resolver=[Mono.Cecil.DefaultAssemblyResolver]::new()
$resolver.AddSearchDirectory((Split-Path -Parent $Assembly))
$resolver.AddSearchDirectory((Split-Path -Parent $Cecil))
$managed=[IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $Cecil) '../../../Managed'))
$resolver.AddSearchDirectory($managed)
$resolver.AddSearchDirectory((Join-Path $managed 'UnityEngine'))
$parameters=[Mono.Cecil.ReaderParameters]::new();$parameters.AssemblyResolver=$resolver
$game=[Mono.Cecil.AssemblyDefinition]::ReadAssembly($Assembly,$parameters)
$inputAsm=[Mono.Cecil.AssemblyDefinition]::ReadAssembly($InputSystemAssembly,$parameters)
try {
    $method=($game.MainModule.Types | Where-Object Name -eq 'InputManager').Methods | Where-Object {
        $_.Name -eq 'GetBindingString' -and $_.Parameters.Count -eq 1 -and $_.Parameters[0].ParameterType.FullName -eq 'System.String'
    }
    if(!$method){throw 'InputManager.GetBindingString(string) schema changed'}
    $body=$method.Body
    $display=@($body.Instructions | Where-Object { $_.Operand -is [Mono.Cecil.MethodReference] -and $_.Operand.Name -eq 'ToDisplayString' })
    if($display.Count -ne 2){throw 'Unexpected binding display call count'}
    if(@($display | Where-Object {$_.Previous.OpCode.Code -eq 'Ldnull'}).Count -eq 0){Write-Output 'Switch prompt patch already applied';return}
    $control=$body.Variables | Where-Object {$_.VariableType.FullName -eq 'UnityEngine.InputSystem.InputControl'} | Select-Object -First 1
    $scheme=$body.Variables | Where-Object {$_.VariableType.FullName -eq 'UnityEngine.InputSystem.InputControlScheme'} | Select-Object -First 1
    $find=$body.Instructions | Where-Object {$_.Operand -is [Mono.Cecil.MethodReference] -and $_.Operand.FullName -match 'InputSystem::FindControl\('} | Select-Object -First 1
    $skip=$find.Next.Next.Next.Operand
    $supports=$body.Instructions | Where-Object {$_.Operand -is [Mono.Cecil.MethodReference] -and $_.Operand.FullName -match 'InputControlScheme::SupportsDevice\('} | Select-Object -First 1
    $device=$body.Instructions | Where-Object {$_.Operand -is [Mono.Cecil.MethodReference] -and $_.Operand.FullName -match 'InputControl::get_device\('} | Select-Object -First 1
    $composite=$body.Instructions | Where-Object {$_.Operand -is [Mono.Cecil.MethodReference] -and $_.Operand.Name -eq 'get_isPartOfComposite'} | Select-Object -First 1
    if(!$control -or !$scheme -or !$supports -or !$device -or !$composite -or $skip -isnot [Mono.Cecil.Cil.Instruction]){throw 'Binding resolver control-flow schema changed'}
    foreach($call in $display){
        if($call.Previous.OpCode.Code -ne 'Ldnull'){throw 'Partially patched binding resolver'}
        $call.Previous.OpCode=[Mono.Cecil.Cil.OpCodes]::Ldloc;$call.Previous.Operand=$control
    }
    # Check the control scheme before entering keyboard composites. Otherwise
    # WASD returns early even when the active device is our Switch gamepad.
    $il=$body.GetILProcessor();$at=$composite.Previous
    $insert=@(
        [Mono.Cecil.Cil.Instruction]::Create([Mono.Cecil.Cil.OpCodes]::Ldloca,$scheme),
        [Mono.Cecil.Cil.Instruction]::Create([Mono.Cecil.Cil.OpCodes]::Ldloc,$control),
        [Mono.Cecil.Cil.Instruction]::Create([Mono.Cecil.Cil.OpCodes]::Callvirt,$device.Operand),
        [Mono.Cecil.Cil.Instruction]::Create([Mono.Cecil.Cil.OpCodes]::Call,$supports.Operand),
        [Mono.Cecil.Cil.Instruction]::Create([Mono.Cecil.Cil.OpCodes]::Brfalse,$skip)
    )
    foreach($instruction in $insert){$il.InsertBefore($at,$instruction)}
    $effective=($inputAsm.MainModule.Types | Where-Object FullName -eq 'UnityEngine.InputSystem.InputBinding').Methods | Where-Object Name -eq 'get_effectivePath'
    foreach($instruction in $body.Instructions){
        if($instruction.Operand -is [Mono.Cecil.MethodReference] -and $instruction.Operand.FullName -match 'InputBinding::get_path\('){$instruction.Operand=$game.MainModule.ImportReference($effective)}
    }
    # Added instructions can move short-branch targets outside the signed-byte range.
    foreach($instruction in $body.Instructions){
        $long=@{Br_S='Br';Brfalse_S='Brfalse';Brtrue_S='Brtrue';Beq_S='Beq';Bne_Un_S='Bne_Un';Ble_S='Ble';Blt_S='Blt';Bge_S='Bge';Bgt_S='Bgt';Leave_S='Leave'}[$instruction.OpCode.Code.ToString()]
        if($long){$instruction.OpCode=[Mono.Cecil.Cil.OpCodes]::$long}
    }
    $backup=$Assembly+'.before-switch-prompts'
    if(!(Test-Path -LiteralPath $backup)){Copy-Item -LiteralPath $Assembly -Destination $backup}
    $temp=$Assembly+'.nx-tmp';$game.Write($temp)
} finally {$game.Dispose();$inputAsm.Dispose()}
Move-Item -LiteralPath $temp -Destination $Assembly -Force
Write-Output 'Patched real binding resolver: device labels, active scheme before composites, effective remaps'
