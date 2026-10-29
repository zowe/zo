#Requires -Version 5.1
<#
zx.ps1 - zo-over-SSH client (Windows PowerShell 5.1+, also runs on PowerShell 7)

  zx install [<dir>]                       write a zx.cmd shim into <dir> (default ~\.local\bin)
  zx uninstall [<dir>]                     remove the shim
  zx deploy <ssh-target> [<remote-dir>]    sftp server.pax.Z, unpax, verify; saves config
  zx use    <ssh-target> <remote-bin>      save host+bin without deploying
  zx start  [<ssh-target> <remote-bin>]    start a persistent JSON-RPC session (faster)
  zx stop                                  stop the persistent session
  zx reset                                 stop + wipe the state dir
  zx info                                  show config + session state

  zx ds   list <pattern> | members <dsn> | read <dsn> | write <dsn> <file>
          | get [--binary] <dsn[(m)]> <localdir|file> | put [--binary] <localdir|file> <dsn[(m)]>
          | create <dsn> [<attrs-json>] | delete <dsn> | copy <src> <dst> [--ow|-r] | rename <a> <b>
  zx job  list [<owner> [<prefix>]] | submit <file|/uss-path|DSN[(MBR)]> | status <id> | spools <id>
          | spool <id> <n> | jcl <id> | cancel|delete|hold|release <id>
  zx uss  ls <path> | get <path> [<local>] | put <local> <path>      (sftp, binary-safe)
          | read <path> | write <path> <file>                         (RPC, text/b64)
          | rm <path> | mkdir <path> | mv <a> <b> | cp <a> <b>
          | chmod <mode> <p> | chown <o> <p> | chtag <t> <p> | sh '<cmd>'
  zx tso  '<cmd>'
  zx system apf | linklist | proclib | parmlib | subsystems | symbol <s> | syslog
  zx tool amblist <dsn> --cs '<stmts>' | run <pgm> [opts] | search <dsn> <str> [opts]
          | dynalloc '<parm>' | dsect --ad <dsn> --cd <dsn>
  zx console '<cmd>' [--cn <name>] [--timeout <s>] [--no-wait]
  zx rpc  <method> ['<params-json>']       raw JSON-RPC, always emits raw JSON
  zx check                                 verify local prereqs

Global flags (anywhere): -j/--json   emit raw JSON instead of pretty output

Prereqs (local):  ssh + sftp on PATH (Windows OpenSSH client). No jq/base64 needed -
                  JSON and base64 are handled in-process.
Prereqs (remote): SSH login + a writable USS directory. zo is self-contained.

Bundle: $env:ZX_PAX (default %LOCALAPPDATA%\zx\server.pax.Z); auto-downloaded on first deploy.
        Must match the SHA-256 in bundle.pin (next to this script) or deploy refuses it.
State:  $env:ZX_STATE (default %TEMP%\zx.%USERNAME%) holds config.json, pid, ssh-pid, ready, err.
Extra ssh/sftp options: $env:ZX_SSH_OPTS (space-separated, e.g. '-i C:\keys\id_rsa').
Response timeout: $env:ZX_TIMEOUT seconds (default 60).

Windows note: Windows OpenSSH has no ControlMaster, so every one-shot call opens its own
SSH connection. 'zx start' holds one connection open in a background host process reachable
over a named pipe - that is the Windows stand-in for connection multiplexing. With password
(not key) auth, prefer 'zx start' so you authenticate once.
#>

$ErrorActionPreference = 'Stop'

# ---- globals ---------------------------------------------------------------
$script:Self = $PSCommandPath
if (-not $script:Self) { $script:Self = $MyInvocation.MyCommand.Path }
$script:Json = $false
$script:StateDir = $env:ZX_STATE
if (-not $script:StateDir) {
  $tmpDir = $env:TEMP
  if (-not $tmpDir) { $tmpDir = [IO.Path]::GetTempPath() }
  $who = $env:USERNAME
  if (-not $who) { $who = 'user' }
  $script:StateDir = Join-Path $tmpDir "zx.$who"
}
$script:CfgFile   = Join-Path $script:StateDir 'config.json'
$script:PidFile   = Join-Path $script:StateDir 'pid'
$script:ReadyFile = Join-Path $script:StateDir 'ready'
$script:ErrFile   = Join-Path $script:StateDir 'err'
$script:Pax = $env:ZX_PAX
if (-not $script:Pax) {
  $appData = $env:LOCALAPPDATA
  if (-not $appData) { $appData = Join-Path $HOME '.local\share' }
  $script:Pax = Join-Path $appData 'zx\server.pax.Z'
}
$script:PinFile = Join-Path (Split-Path -Parent $script:Self) 'bundle.pin'
$script:Timeout = 60
if ($env:ZX_TIMEOUT -and ($env:ZX_TIMEOUT -match '^\d+$')) { $script:Timeout = [int]$env:ZX_TIMEOUT }
$script:Utf8  = New-Object System.Text.UTF8Encoding($false)
$script:ZHost = $null
$script:ZBin  = $null

# Say = normal output. Goes through the pipeline (not [Console]) so `zx ... > f`,
# `zx ... | ...` and `$x = zx ...` all behave like any other PowerShell command.
function Say([string]$msg) { Write-Output $msg }
function Note([string]$msg) { [Console]::Error.WriteLine("zx: $msg") }
function Die([string]$msg) { [Console]::Error.WriteLine("zx: $msg"); exit 1 }

function New-Dir([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
}
function Write-TextFile([string]$path, [string]$text) {
  [IO.File]::WriteAllText($path, $text, $script:Utf8)
}
function Get-FullPath([string]$path) {
  if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }
  return [IO.Path]::GetFullPath((Join-Path (Get-Location).ProviderPath $path))
}
function Get-HostExe {
  try {
    $exe = [Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
    if ($exe) { return $exe }
  } catch { }
  $p = Get-Process -Id $PID -ErrorAction SilentlyContinue
  if ($p -and $p.Path) { return $p.Path }
  return (Join-Path $PSHOME 'powershell.exe')
}

# ---- quoting ---------------------------------------------------------------
# Windows CommandLineToArgvW quoting, for ProcessStartInfo.Arguments
# (.NET Framework's ProcessStartInfo has no ArgumentList collection).
function Quote-WinArg([string]$a) {
  if ($null -eq $a) { $a = '' }
  if ($a -ne '' -and $a -notmatch '[\s"]') { return $a }
  $sb = New-Object Text.StringBuilder
  [void]$sb.Append('"')
  for ($i = 0; $i -lt $a.Length; $i++) {
    $bs = 0
    while ($i -lt $a.Length -and $a[$i] -eq '\') { $bs++; $i++ }
    if ($i -ge $a.Length) { [void]$sb.Append('\' * ($bs * 2)); break }
    elseif ($a[$i] -eq '"') { [void]$sb.Append('\' * ($bs * 2 + 1)); [void]$sb.Append('"') }
    else { [void]$sb.Append('\' * $bs); [void]$sb.Append($a[$i]) }
  }
  [void]$sb.Append('"')
  return $sb.ToString()
}
function Quote-WinArgs([string[]]$argv) {
  $parts = @()
  foreach ($a in $argv) { $parts += (Quote-WinArg $a) }
  return ($parts -join ' ')
}
# POSIX single-quote, for the *remote* shell.
function Quote-Sh([string]$a) { return "'" + ($a -replace "'", "'\''") + "'" }
# sftp batch-line quoting (sftp understands double quotes).
function Quote-Sftp([string]$p) { return '"' + ($p -replace '"', '\"') + '"' }
# sftp treats "\" as an escape character, so local paths go in with forward slashes.
function Quote-SftpLocal([string]$p) { return (Quote-Sftp ($p -replace '\\', '/')) }

# ---- child processes -------------------------------------------------------
# Runs a program to completion. The std handles are redirected but the console is
# inherited, so ssh can still prompt for a password/passphrase on the terminal.
function Invoke-Proc {
  param([string]$Exe, [string[]]$ProcArgs, [string]$StdinText, [switch]$PassStderr)
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName               = $Exe
  $psi.Arguments              = Quote-WinArgs $ProcArgs
  $psi.UseShellExecute        = $false
  $psi.CreateNoWindow         = $true
  $psi.RedirectStandardInput  = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError  = $true
  $psi.StandardOutputEncoding = $script:Utf8
  $psi.StandardErrorEncoding  = $script:Utf8
  $proc = New-Object Diagnostics.Process
  $proc.StartInfo = $psi
  try { [void]$proc.Start() } catch { Die "cannot run '$Exe' - $($_.Exception.Message)" }
  # Start the readers before writing stdin so a chatty child cannot deadlock us.
  $outTask = $proc.StandardOutput.ReadToEndAsync()
  $errTask = $proc.StandardError.ReadToEndAsync()
  try {
    if ($StdinText) {
      $bytes = $script:Utf8.GetBytes($StdinText)
      $proc.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
      $proc.StandardInput.BaseStream.Flush()
    }
    $proc.StandardInput.Close()
  } catch { }
  $proc.WaitForExit()
  $so = $outTask.Result
  $se = $errTask.Result
  if ($PassStderr -and $se) { [Console]::Error.Write($se) }
  return (New-Object psobject -Property @{ Out = $so; Err = $se; Code = $proc.ExitCode })
}

function Get-SshOpts {
  $opts = @()
  if ($env:ZX_SSH_OPTS) { $opts += ($env:ZX_SSH_OPTS -split '\s+' | Where-Object { $_ }) }
  return $opts
}
function Invoke-Ssh {
  param([string[]]$SshArgs, [string]$StdinText, [switch]$PassStderr)
  $a = @(@(Get-SshOpts) + @('-T') + $SshArgs)
  return (Invoke-Proc -Exe 'ssh' -ProcArgs $a -StdinText $StdinText -PassStderr:$PassStderr)
}
# $Batch is sftp batch input; lines must be LF-separated and end with 'bye'.
# `sftp -b` implies BatchMode=yes (no password prompts); ssh honours the *first*
# value seen for an option, so ZX_SSH_OPTS still wins if the user sets it.
function Invoke-Sftp {
  param([string]$Target, [string]$Batch)
  $a = @(@(Get-SshOpts) + @('-o', 'BatchMode=no', '-b', '-', $Target))
  $r = Invoke-Proc -Exe 'sftp' -ProcArgs $a -StdinText $Batch
  if ($r.Code -ne 0) {
    [Console]::Error.Write($r.Err)
    Die "sftp failed (exit $($r.Code))"
  }
  return $r
}

# ---- config ----------------------------------------------------------------
function Load-Cfg {
  $script:ZHost = $env:ZX_HOST
  $script:ZBin  = $env:ZX_BIN
  if (Test-Path -LiteralPath $script:CfgFile) {
    try {
      $c = Get-Content -LiteralPath $script:CfgFile -Raw | ConvertFrom-Json
      if (-not $script:ZHost) { $script:ZHost = $c.host }
      if (-not $script:ZBin)  { $script:ZBin  = $c.bin }
    } catch { Note "ignoring unreadable config $($script:CfgFile)" }
  }
  if (-not $script:ZHost) { Die "no host configured - run 'zx deploy <host>' or 'zx use <host> <bin>' (or set ZX_HOST/ZX_BIN)" }
  if (-not $script:ZBin)  { Die "no zo path configured - run 'zx use <host> <bin>' (or set ZX_BIN)" }
}
function Save-Cfg([string]$h, [string]$b) {
  New-Dir $script:StateDir
  $obj = New-Object psobject -Property @{ host = $h; bin = $b }
  Write-TextFile $script:CfgFile ($obj | ConvertTo-Json -Compress)
}

# ---- bundle auto-download --------------------------------------------------
# bundle.pin (next to this script) pins the release asset and its SHA-256;
# nothing is deployed unless the bundle matches it.
function Get-BundlePin {
  if (-not (Test-Path -LiteralPath $script:PinFile)) { Die "bundle pin not found: $($script:PinFile)" }
  $pin = @{}
  foreach ($line in (Get-Content -LiteralPath $script:PinFile)) {
    if ($line -match '^\s*(tag|asset|sha256)\s*=\s*(\S+)') { $pin[$Matches[1]] = $Matches[2] }
  }
  if (-not $pin['tag'] -or -not $pin['asset']) { Die "$($script:PinFile) must set tag= and asset=" }
  if ($pin['sha256'] -notmatch '^[0-9a-fA-F]{64}$') { Die "$($script:PinFile): sha256= must be 64 hex digits" }
  $pin['sha256'] = $pin['sha256'].ToLowerInvariant()
  return $pin
}
function Get-FileSha256([string]$path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  $fs = [IO.File]::OpenRead((Get-FullPath $path))
  try { $h = $sha.ComputeHash($fs) } finally { $fs.Dispose(); $sha.Dispose() }
  return (-join ($h | ForEach-Object { $_.ToString('x2') }))
}
function Test-BundleHash([string]$path, [string]$want) {
  $got = Get-FileSha256 $path
  if ($got -eq $want) { return $true }
  Note "sha256 mismatch for $path"
  Note "  expected $want (pinned in $($script:PinFile))"
  Note "  actual   $got"
  return $false
}
function Ensure-Bundle {
  $pin = Get-BundlePin
  if (Test-Path -LiteralPath $script:Pax) {
    if (-not (Test-BundleHash $script:Pax $pin.sha256)) {
      Die "delete it to re-download the pinned bundle, or update sha256= in $($script:PinFile) to deploy this build"
    }
    return
  }
  try {
    [Net.ServicePointManager]::SecurityProtocol =
      [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
  } catch { }
  $url = "https://github.com/zowe/zowex/releases/download/$($pin.tag)/$($pin.asset)"
  Say "zx: bundle not found; downloading $($pin.asset) ($($pin.tag)) to $($script:Pax)"
  New-Dir (Split-Path -Parent $script:Pax)
  $part = "$($script:Pax).part"
  try { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $part }
  catch { Die "download failed: $url - $($_.Exception.Message)" }
  if (-not (Test-BundleHash $part $pin.sha256)) {
    Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue
    Die 'refusing to deploy an unverified bundle'
  }
  Move-Item -LiteralPath $part -Destination $script:Pax -Force
  Say "zx: sha256 verified; saved to $($script:Pax)"
}

# ---- persistent session (named-pipe host) ---------------------------------
function Get-PipeName {
  # SHA256, not MD5: MD5 throws when Windows FIPS policy is enforced.
  $sha = [Security.Cryptography.SHA256]::Create()
  $h = $sha.ComputeHash($script:Utf8.GetBytes($script:StateDir.ToLowerInvariant()))
  $hex = -join ($h[0..7] | ForEach-Object { $_.ToString('x2') })
  return "zx-$hex"
}
function Get-SessionPid {
  if (-not (Test-Path -LiteralPath $script:PidFile)) { return 0 }
  $raw = (Get-Content -LiteralPath $script:PidFile -Raw).Trim()
  if ($raw -notmatch '^\d+$') { return 0 }
  return [int]$raw
}
function Test-Live {
  $sp = Get-SessionPid
  if ($sp -le 0) { return $false }
  $proc = Get-Process -Id $sp -ErrorAction SilentlyContinue
  if (-not $proc) { return $false }
  # Guard against PID reuse: the session host is always a PowerShell process.
  return ($proc.ProcessName -match '^(powershell|pwsh)$')
}
function Send-Pipe([string]$line, [int]$connectMs = 5000) {
  $cuo = Get-PipeOption 'CurrentUserOnly'
  $copts = [IO.Pipes.PipeOptions]::None
  if ($null -ne $cuo) { $copts = $cuo }
  $pipe = New-Object IO.Pipes.NamedPipeClientStream('.', (Get-PipeName), [IO.Pipes.PipeDirection]::InOut, $copts)
  try {
    try { $pipe.Connect($connectMs) } catch { Die "cannot reach the session on pipe $(Get-PipeName) - try 'zx stop' then 'zx start'" }
    if ($null -eq $cuo) {
      $me    = [Security.Principal.WindowsIdentity]::GetCurrent().User
      $owner = $pipe.GetAccessControl().GetOwner([Security.Principal.SecurityIdentifier])
      if ($owner -ne $me) { Die "pipe $(Get-PipeName) is not owned by the current user; refusing to talk to it" }
    }
    $w = New-Object IO.StreamWriter($pipe, $script:Utf8)
    $w.AutoFlush = $true
    $w.NewLine = "`n"
    $r = New-Object IO.StreamReader($pipe, $script:Utf8)
    $w.WriteLine($line)
    $t = $r.ReadLineAsync()
    if (-not $t.Wait($script:Timeout * 1000)) { Die "session timed out after $($script:Timeout)s (raise ZX_TIMEOUT)" }
    return $t.Result
  } finally { $pipe.Dispose() }
}

function Start-Session {
  Load-Cfg
  if (Test-Live) { Die "session already running (pid $(Get-SessionPid)) - 'zx stop' first" }
  New-Dir $script:StateDir
  Remove-Item -LiteralPath $script:ReadyFile, $script:ErrFile, $script:PidFile -Force -ErrorAction SilentlyContinue
  # Start-Process joins -ArgumentList with plain spaces, so quote it ourselves.
  $cmdline = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', (Quote-WinArg $script:Self),
    '__serve',
    (Quote-WinArg $script:StateDir),
    (Quote-WinArg $script:ZHost),
    (Quote-WinArg $script:ZBin)
  ) -join ' '
  # -NoNewWindow keeps the console attached so ssh can still prompt for a password.
  $proc = Start-Process -FilePath (Get-HostExe) -ArgumentList $cmdline -NoNewWindow -PassThru
  Write-TextFile $script:PidFile ([string]$proc.Id)
  # Generous: with password auth the ssh child prompts on the shared console here.
  $deadline = (Get-Date).AddSeconds([Math]::Max(60, $script:Timeout))
  while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $script:ReadyFile) {
      $banner = (Get-Content -LiteralPath $script:ReadyFile -Raw).Trim()
      if ($banner -like '*"status":"ready"*') {
        $ver = ''
        if ($banner -match '"version":"([^"]*)"') { $ver = $Matches[1] }
        Say "zx: ready ($ver)"
      } else {
        Note "unexpected banner: $banner"
      }
      return
    }
    if ($proc.HasExited) { break }
    Start-Sleep -Milliseconds 250
  }
  $why = ''
  if (Test-Path -LiteralPath $script:ErrFile) { $why = (Get-Content -LiteralPath $script:ErrFile -Raw) }
  Stop-Session
  if ($why) { [Console]::Error.Write($why) }
  Die 'server failed to start'
}

function Stop-Session {
  if (Test-Live) {
    $sp = Get-SessionPid
    try { [void](Send-Pipe '__shutdown' 2000) } catch { }
    for ($i = 0; $i -lt 20; $i++) {
      if (-not (Get-Process -Id $sp -ErrorAction SilentlyContinue)) { break }
      Start-Sleep -Milliseconds 100
    }
    Stop-Process -Id $sp -Force -ErrorAction SilentlyContinue
  }
  # Reap the ssh child too: killing the host process does not take it down.
  $sshPidFile = Join-Path $script:StateDir 'ssh-pid'
  if (Test-Path -LiteralPath $sshPidFile) {
    $raw = (Get-Content -LiteralPath $sshPidFile -Raw).Trim()
    if ($raw -match '^\d+$') {
      $sshProc = Get-Process -Id ([int]$raw) -ErrorAction SilentlyContinue
      if ($sshProc -and $sshProc.ProcessName -eq 'ssh') { Stop-Process -Id ([int]$raw) -Force -ErrorAction SilentlyContinue }
    }
  }
  Remove-Item -LiteralPath $script:PidFile, $script:ReadyFile, $script:ErrFile, $sshPidFile -Force -ErrorAction SilentlyContinue
}

function Get-PipeOption([string]$name) {
  $v = $null
  if ([Enum]::TryParse([IO.Pipes.PipeOptions], $name, [ref]$v)) { return $v }
  return $null
}
function New-PipeServer([string]$name) {
  # Fails closed: the pipe carries commands that run on z/OS, so never fall back to a default DACL.
  $dir  = [IO.Pipes.PipeDirection]::InOut
  $mode = [IO.Pipes.PipeTransmissionMode]::Byte
  $opts = [IO.Pipes.PipeOptions]::None
  $first = Get-PipeOption 'FirstPipeInstance'
  if ($null -ne $first) { $opts = $opts -bor $first }
  $cuo = Get-PipeOption 'CurrentUserOnly'
  if ($null -ne $cuo) {
    return (New-Object IO.Pipes.NamedPipeServerStream($name, $dir, 1, $mode, ($opts -bor $cuo)))
  }
  $sec  = New-Object IO.Pipes.PipeSecurity
  $me   = [Security.Principal.WindowsIdentity]::GetCurrent().User
  $rule = New-Object IO.Pipes.PipeAccessRule(
    $me, [IO.Pipes.PipeAccessRights]::FullControl, [Security.AccessControl.AccessControlType]::Allow)
  $sec.AddAccessRule($rule)
  return (New-Object IO.Pipes.NamedPipeServerStream($name, $dir, 1, $mode, $opts, 0, 0, $sec))
}

# The background session host: owns one long-lived `zo server` over ssh and
# answers one JSON-RPC request per named-pipe connection.
function Invoke-Serve([string]$stateDir, [string]$zhost, [string]$zbin) {
  $script:StateDir  = $stateDir
  $script:ReadyFile = Join-Path $stateDir 'ready'
  $script:ErrFile   = Join-Path $stateDir 'err'
  $sshErrFile       = Join-Path $stateDir 'ssh-err'
  $proc = $null
  try {
    if (-not $zhost -or -not $zbin) { throw 'session host started without host/bin' }
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName               = 'ssh'
    $psi.Arguments              = Quote-WinArgs @(@(Get-SshOpts) + @('-T', $zhost, "$zbin server -w 1"))
    $psi.UseShellExecute        = $false
    $psi.CreateNoWindow         = $true
    $psi.RedirectStandardInput  = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.StandardOutputEncoding = $script:Utf8
    $psi.StandardErrorEncoding  = $script:Utf8
    $proc = New-Object Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()
    # Record the ssh pid: if this host is killed outright, 'zx stop' still has to
    # be able to reap the ssh child.
    Write-TextFile (Join-Path $stateDir 'ssh-pid') ([string]$proc.Id)
    # Drain stderr into a task so a noisy server cannot block on a full pipe.
    $errTask = $proc.StandardError.ReadToEndAsync()
    $banner = $proc.StandardOutput.ReadLine()
    if (-not $banner) {
      $detail = ''
      if ($errTask.Wait(2000)) { $detail = $errTask.Result }
      throw "no ready banner from zo. $detail"
    }
    Write-TextFile $script:ReadyFile $banner
    $pipeName = Get-PipeName
    $pipe = New-PipeServer $pipeName
    while ($true) {
      if ($proc.HasExited) { break }
      $stop = $false
      try {
        $pipe.WaitForConnection()
        $r = New-Object IO.StreamReader($pipe, $script:Utf8)
        $w = New-Object IO.StreamWriter($pipe, $script:Utf8)
        $w.AutoFlush = $true
        $w.NewLine = "`n"
        $req = $r.ReadLine()
        if ($req) {
          if ($req -eq '__shutdown') {
            $w.WriteLine('{"zx":"stopping"}')
            $stop = $true
          } else {
            $bytes = $script:Utf8.GetBytes($req + "`n")
            $proc.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
            $proc.StandardInput.BaseStream.Flush()
            $t = $proc.StandardOutput.ReadLineAsync()
            if (-not $t.Wait($script:Timeout * 1000)) {
              $w.WriteLine('{"jsonrpc":"2.0","error":{"code":-32000,"message":"zx: timed out waiting for the zo server"}}')
            } elseif ($null -eq $t.Result) {
              $w.WriteLine('{"jsonrpc":"2.0","error":{"code":-32000,"message":"zx: the zo server closed the connection"}}')
              $stop = $true
            } else {
              $w.WriteLine($t.Result)
            }
          }
        }
      } finally {
        try { if ($pipe.IsConnected) { $pipe.Disconnect() } } catch { }
      }
      if ($stop) { break }
    }
    try { $pipe.Dispose() } catch { }
    try { $proc.Kill() } catch { }
    if ($errTask.Wait(2000)) { Write-TextFile $sshErrFile $errTask.Result }
  } catch {
    try { Write-TextFile $script:ErrFile "zx: session host: $($_.Exception.Message)`n" } catch { }
  } finally {
    if ($proc) { try { if (-not $proc.HasExited) { $proc.Kill() } } catch { } }
  }
  exit 0
}

# ---- RPC dispatch (persistent or one-shot) --------------------------------
function To-Json($o) { return ($o | ConvertTo-Json -Compress -Depth 20) }

function Invoke-Rpc {
  param([string]$Method, $Params)
  if ($null -eq $Params) { $Params = '{}' }
  if ($Params -isnot [string]) { $Params = To-Json $Params }
  if (-not "$Params".Trim()) { $Params = '{}' }
  $req = '{"jsonrpc":"2.0","id":1,"method":"' + $Method + '","params":' + $Params + '}'
  if (Test-Live) { return (Send-Pipe $req) }
  Load-Cfg
  $r = Invoke-Ssh -SshArgs @($script:ZHost, "$($script:ZBin) server -w 1") -StdinText ($req + "`n")
  # Drop the ready banner. zo sometimes reports errors on stderr instead.
  $resp = Skip-FirstLine $r.Out
  if (-not $resp.Trim() -and $r.Err) { $resp = $r.Err }
  return $resp.Trim()
}
function Skip-FirstLine([string]$s) {
  if (-not $s) { return '' }
  $lines = $s -split "`r?`n"
  if ($lines.Count -le 1) { return '' }
  return (($lines[1..($lines.Count - 1)]) -join "`n")
}

# ---- CLI passthrough (for tool/non-RPC system ops) ------------------------
function Invoke-Cli([string[]]$CliArgs) {
  Load-Cfg
  $cmd = Quote-Sh $script:ZBin
  foreach ($a in $CliArgs) { $cmd += ' ' + (Quote-Sh $a) }
  $r = Invoke-Ssh -SshArgs @($script:ZHost, $cmd) -PassStderr
  if ($r.Out) { Say ($r.Out -replace "`r?`n$", '') }
  if ($r.Code -ne 0) { exit $r.Code }
}

# ---- response handling -----------------------------------------------------
function From-Json([string]$s) {
  if (-not $s -or -not $s.Trim()) { return $null }
  # A one-shot call can emit more than one line (e.g. a trailing diagnostic).
  foreach ($line in ($s -split "`r?`n")) {
    $t = $line.Trim()
    if ($t.StartsWith('{')) {
      try { return ($t | ConvertFrom-Json) } catch { }
    }
  }
  return $null
}
function Has-Prop($o, [string]$name) {
  if ($null -eq $o) { return $false }
  return ((@($o.PSObject.Properties.Name) -contains $name))
}
function Decode-B64([string]$b64) {
  if (-not $b64) { return (New-Object byte[] 0) }
  return [Convert]::FromBase64String(($b64 -replace '\s', ''))
}
function Format-Row([string[]]$vals, [int[]]$widths) {
  $s = ''
  for ($i = 0; $i -lt $vals.Count; $i++) {
    $v = [string]$vals[$i]
    $w = $widths[$i]
    if ($w -lt 0) { $s += $v.PadRight(-$w) } else { $s += $v.PadLeft($w) }
    $s += '  '
  }
  return $s.TrimEnd()
}
# Returns an error message, or $null when the response is a success.
function Get-RpcError([string]$resp) {
  $o = From-Json $resp
  if ($null -eq $o) { return "no or unparseable response from server: $($resp.Trim())" }
  if (Has-Prop $o 'error') {
    $m = $o.error.message
    if (-not $m) { $m = 'unknown error' }
    if ((Has-Prop $o.error 'data') -and $o.error.data) { $m += " ($($o.error.data))" }
    return $m
  }
  if ((Has-Prop $o.result 'success') -and (-not $o.result.success)) {
    $m = $o.result.message
    if (-not $m) { $m = 'request failed' }
    return $m
  }
  return $null
}

# $Mode: kv | table | b64 | text ; $Cols entries look like 'id:-8' (negative = left-align)
function Out-Resp {
  param([string]$Resp, [string]$Mode = 'kv', [string[]]$Cols = @())
  if ($script:Json) { Say ($Resp.Trim()); return }
  $o = From-Json $Resp
  if ($null -eq $o) { Die "no or unparseable response from server: $($Resp.Trim())" }
  if (Has-Prop $o 'error') {
    $msg = $o.error.message
    if (-not $msg) { $msg = 'unknown error' }
    Note "error: $msg"
    if ((Has-Prop $o.error 'data') -and $o.error.data) { [Console]::Error.WriteLine([string]$o.error.data) }
    exit 1
  }
  $res = $o.result
  if ($null -eq $res) { Die "response has no result: $($Resp.Trim())" }
  switch ($Mode) {
    'b64' {
      $txt = [Text.Encoding]::UTF8.GetString((Decode-B64 $res.data))
      Say ($txt -replace "`r?`n$", '')
    }
    'text' {
      $txt = [string]$res.data
      Say ($txt -replace "`r?`n$", '')
    }
    'kv' {
      foreach ($kv in $res.PSObject.Properties) {
        if ($kv.Name -eq 'success') { continue }
        Say "$($kv.Name)=$($kv.Value)"
      }
    }
    'table' {
      $items = @()
      if ((Has-Prop $res 'items') -and $res.items) { $items = @($res.items) }
      if ($Cols.Count -eq 0) {
        foreach ($it in $items) {
          if ($it -is [psobject] -and $it -isnot [string] -and $it -isnot [ValueType]) {
            Say ((@($it.PSObject.Properties.Value) | ForEach-Object { [string]$_ }) -join '  ')
          } else {
            Say ([string]$it)
          }
        }
      } else {
        $names = @()
        $widths = @()
        foreach ($c in $Cols) {
          if ($c -match '^(.*):(-?\d+)$') { $names += $Matches[1]; $widths += [int]$Matches[2] }
          else { $names += $c; $widths += -12 }
        }
        Say (Format-Row (@($names | ForEach-Object { $_.ToUpperInvariant() })) $widths)
        foreach ($it in $items) {
          $row = @()
          foreach ($n in $names) {
            $v = $it.$n
            if ($null -eq $v) { $v = '' }
            $row += [string]$v
          }
          Say (Format-Row $row $widths)
        }
      }
    }
  }
}

# ---- dataset/file transfer helpers ----------------------------------------
# readDataset/writeDataset params; "encoding":"binary" skips EBCDIC conversion.
function New-DsParams([string]$dsn, [bool]$binary) {
  $h = @{ dsname = $dsn }
  if ($binary) { $h['encoding'] = 'binary' }
  return $h
}
function Get-FileB64([string]$path) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { Die "local file not found: $path" }
  return [Convert]::ToBase64String([IO.File]::ReadAllBytes((Get-FullPath $path)))
}
function Save-RespB64([string]$resp, [string]$dst) {
  $err = Get-RpcError $resp
  if ($err) { Die "error: $err" }
  $o = From-Json $resp
  [IO.File]::WriteAllBytes($dst, (Decode-B64 $o.result.data))
}

# ---- Data set member name and path validation ---------------------------------------
function Get-MemberOutPath([string]$dir, [string]$name) {
  # z/OS member name: 1-8 chars, first alphabetic or @ # $, rest alphanumeric or @ # $.
  if ($name -notmatch '^[A-Za-z@#$][A-Za-z0-9@#$]{0,7}$') {
    Die "server returned an invalid PDS member name: '$name'"
  }
  $root = [IO.Path]::GetFullPath($dir)
  if (-not $root.EndsWith([string][IO.Path]::DirectorySeparatorChar)) { $root += [IO.Path]::DirectorySeparatorChar }
  $out = [IO.Path]::GetFullPath((Join-Path $dir $name))
  if (-not $out.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
    Die "server returned a member name that escapes $dir : '$name'"
  }
  return $out
}

# ---- usage -----------------------------------------------------------------
function Get-Usage {
  $txt = Get-Content -LiteralPath $script:Self -Raw
  if ($txt -match '(?s)<\#(.*?)\#>') { return $Matches[1].Trim() }
  return 'zx.ps1 - see the zo-ssh skill'
}
function Show-GroupHelp([string]$grp) {
  $keep = $false
  foreach ($line in (Get-Usage -split "`r?`n")) {
    if ($line -match "^  zx $([regex]::Escape($grp))\b") { $keep = $true }
    elseif ($keep -and $line -match '^  zx ') { break }
    elseif ($keep -and $line -notmatch '^\s{9,}') { break }
    if ($keep) { Say ($line -replace '^  ', '') }
  }
}

# ---- arg parsing -----------------------------------------------------------
$argv = @()
foreach ($a in @($args)) {
  if ($a -eq '-j' -or $a -eq '--json') { $script:Json = $true } else { $argv += $a }
}
$grp = ''
if ($argv.Count -gt 0) { $grp = [string]$argv[0] }
$rest = @()
if ($argv.Count -gt 1) { $rest = @($argv[1..($argv.Count - 1)]) }

if ($grp -eq '' -or $grp -eq 'help' -or $grp -eq '-h' -or $grp -eq '--help') { Say (Get-Usage); exit 0 }
if ($grp -like '-*') { Die "unknown flag $grp - run 'zx help'" }
if ($rest.Count -ge 1 -and ($rest[0] -eq '-h' -or $rest[0] -eq '--help')) { Show-GroupHelp $grp; exit 0 }

$sub = ''
$p = @()
if ($rest.Count -gt 0) { $sub = [string]$rest[0] }
if ($rest.Count -gt 1) { $p = @($rest[1..($rest.Count - 1)]) }
function Need-Args([int]$n, [string]$usage) { if ($p.Count -ne $n) { Die "usage: $usage" } }
function Need-Min([int]$n, [string]$usage) { if ($p.Count -lt $n) { Die "usage: $usage" } }

switch ($grp) {

'__serve' {
  $sd = ''; $sh = ''; $sb = ''
  if ($rest.Count -ge 1) { $sd = [string]$rest[0] }
  if ($rest.Count -ge 2) { $sh = [string]$rest[1] }
  if ($rest.Count -ge 3) { $sb = [string]$rest[2] }
  Invoke-Serve $sd $sh $sb
}

# ========================================================================= #
'install' {
  $dir = if ($rest.Count -ge 1) { [string]$rest[0] } else { (Join-Path $HOME '.local\bin') }
  New-Dir $dir
  $target = Join-Path $dir 'zx.cmd'
  if (Test-Path -LiteralPath $target) {
    [Console]::Error.Write("zx: $target already exists - overwrite? [y/N] ")
    $ans = [Console]::ReadLine()
    if ($ans -notmatch '^[Yy]') { Die 'cancelled' }
  }
  $shim = "@echo off`r`n" +
          "`"$(Get-HostExe)`" -NoProfile -ExecutionPolicy Bypass -File `"$($script:Self)`" %*`r`n"
  [IO.File]::WriteAllText($target, $shim, (New-Object Text.ASCIIEncoding))
  Say "zx: installed $target -> $($script:Self)"
  $onPath = $false
  foreach ($d in ($env:PATH -split ';')) {
    if ($d -and ($d.TrimEnd('\') -ieq $dir.TrimEnd('\'))) { $onPath = $true }
  }
  if ($onPath) {
    Say "zx: '$dir' is on your PATH - 'zx' is ready to use"
  } else {
    Say 'zx: add it to your PATH for this session:'
    Say "      `$env:PATH = `"$dir;`$env:PATH`""
    Say 'zx: or permanently (takes effect in new shells):'
    Say "      [Environment]::SetEnvironmentVariable('PATH', `"$dir;`" + [Environment]::GetEnvironmentVariable('PATH','User'), 'User')"
  }
}

'uninstall' {
  $dir = if ($rest.Count -ge 1) { [string]$rest[0] } else { (Join-Path $HOME '.local\bin') }
  $target = Join-Path $dir 'zx.cmd'
  if (-not (Test-Path -LiteralPath $target)) { Die "$target not found" }
  Remove-Item -LiteralPath $target -Force
  Say "zx: removed $target"
}

# ========================================================================= #
'deploy' {
  if ($rest.Count -lt 1) { Die 'usage: zx deploy <ssh-target> [<remote-dir>]' }
  $dHost = [string]$rest[0]
  $dirArg = ''
  if ($rest.Count -ge 2) { $dirArg = [string]$rest[1] }
  Ensure-Bundle
  # One ssh round-trip: resolve dir, mkdir, check for an existing binary, report.
  # (sftp does NOT expand $HOME/~, so resolve to an absolute path here.)
  $remoteScript = @'
set -e
d="$1"
[ -n "$d" ] || d="$HOME/zowex"
case "$d" in /*) : ;; "~"*) d="$HOME${d#\~}";; *) d="$HOME/$d";; esac
# cd to the parent first to trigger an automount (mkdir -p cannot); then create the leaf.
p="${d%/*}"; b="${d##*/}"
cd "$p" || exit 9
: > /dev/null
[ -d "$b" ] || mkdir "$b" || exit 9
cd "$b"
if [ -x ./zo ]; then echo "HAVE $PWD"; else echo "NEED $PWD"; fi
'@
  # The remote /bin/sh needs LF-only input even if this file was checked out CRLF.
  $remoteScript = ($remoteScript -replace "`r`n", "`n") + "`n"
  $r = Invoke-Ssh -SshArgs @($dHost, '/bin/sh', '-s', '--', $dirArg) -StdinText $remoteScript -PassStderr
  $probe = ''
  foreach ($line in ($r.Out -split "`r?`n")) {
    if ($line -match '^(HAVE|NEED) ') { $probe = $line.Trim() }
  }
  if ($r.Code -ne 0 -or -not $probe) {
    $shown = $dirArg
    if (-not $shown) { $shown = '$HOME/zowex' }
    Die "cannot create '$shown' on $dHost (see error above)"
  }
  $state = $probe.Split(' ')[0]
  $dir = $probe.Substring($state.Length + 1)
  if ($state -eq 'HAVE') {
    Say "zx: $dir/zo already present on $dHost"
  } else {
    Say "zx: uploading $(Split-Path -Leaf $script:Pax) -> ${dHost}:$dir"
    $batch = "cd $(Quote-Sftp $dir)`n" +
             "put $(Quote-SftpLocal (Get-FullPath $script:Pax)) server.pax.Z`n" +
             "bye`n"
    [void](Invoke-Sftp -Target $dHost -Batch $batch)
    $unpax = Invoke-Ssh -SshArgs @($dHost, "cd $(Quote-Sh $dir) && pax -rvf server.pax.Z && chmod +x zo") -PassStderr
    if ($unpax.Out) { Say ($unpax.Out -replace "`r?`n$", '') }
    if ($unpax.Code -ne 0) { Die "unpax failed on $dHost (exit $($unpax.Code))" }
  }
  Save-Cfg $dHost "$dir/zo"
  $ver = Invoke-Ssh -SshArgs @($dHost, (Quote-Sh "$dir/zo") + ' --version') -PassStderr
  foreach ($line in ($ver.Out -split "`r?`n")) {
    if ($line.Trim()) { Say "zx: $($line.Trim())" }
  }
  Say "zx: configured (host=$dHost bin=$dir/zo)"
}

'use' {
  if ($rest.Count -ne 2) { Die 'usage: zx use <ssh-target> <remote-zo-path>' }
  Save-Cfg ([string]$rest[0]) ([string]$rest[1])
  Say "zx: configured (host=$($rest[0]) bin=$($rest[1]))"
}

'start' {
  if ($rest.Count -ge 2) { Save-Cfg ([string]$rest[0]) ([string]$rest[1]) }
  Start-Session
}

'stop' { Stop-Session }

'reset' {
  Stop-Session
  Remove-Item -LiteralPath $script:StateDir -Recurse -Force -ErrorAction SilentlyContinue
}

'info' {
  Say "state=$($script:StateDir)"
  if (Test-Path -LiteralPath $script:CfgFile) {
    $c = Get-Content -LiteralPath $script:CfgFile -Raw | ConvertFrom-Json
    Say "host=$($c.host)"
    Say "bin=$($c.bin)"
  } else {
    Say 'zx: no config'
  }
  if ($env:ZX_HOST) { Say "ZX_HOST=$($env:ZX_HOST) (env override)" }
  if ($env:ZX_BIN)  { Say "ZX_BIN=$($env:ZX_BIN) (env override)" }
  if (Test-Live) { Say "zx: session live (pid $(Get-SessionPid), pipe $(Get-PipeName))" }
  else { Say 'zx: no session (one-shot mode)' }
}

'check' {
  $ok = $true
  foreach ($c in @('ssh', 'sftp')) {
    $cmd = Get-Command $c -ErrorAction SilentlyContinue
    if ($cmd) { Say "  ok   $c  ($($cmd.Source))" } else { Say "  MISS $c  (install the Windows OpenSSH Client feature)"; $ok = $false }
  }
  Say '  ok   json/base64  (in-process - no jq needed)'
  Say "  PowerShell $($PSVersionTable.PSVersion)"
  if ($PSVersionTable.PSVersion.Major -lt 5) { Say '  MISS PowerShell 5.1 or newer'; $ok = $false }
  $pin = Get-BundlePin
  Say "  ok   pin     $($pin.asset) ($($pin.tag)) sha256 $($pin.sha256)"
  if (-not (Test-Path -LiteralPath $script:Pax)) {
    Say "  miss bundle  not found at $($script:Pax) - 'zx deploy <host>' downloads the pinned release, or set ZX_PAX to it"
  } elseif ((Get-FileSha256 $script:Pax) -eq $pin.sha256) {
    Say "  ok   bundle  $($script:Pax) (sha256 matches pin)"
  } else {
    Say "  BAD  bundle  $($script:Pax) does not match the pinned sha256 - delete it or update bundle.pin"; $ok = $false
  }
  if (-not $ok) { Die 'missing prerequisites' }
}

'rpc' {
  if ($rest.Count -lt 1) { Die "usage: zx rpc <method> ['<params-json>']" }
  $pj = ''
  if ($rest.Count -ge 2) { $pj = [string]$rest[1] }
  Say ((Invoke-Rpc ([string]$rest[0]) $pj).Trim())
}

# ========================================================================= #
'ds' {
  switch ($sub) {
    'list' {
      Need-Args 1 'zx ds list <pattern>'
      Out-Resp (Invoke-Rpc 'listDatasets' @{ pattern = $p[0] }) 'table'
    }
    'members' {
      Need-Args 1 'zx ds members <dsn>'
      Out-Resp (Invoke-Rpc 'listDsMembers' @{ dsname = $p[0] }) 'table'
    }
    'read' {
      Need-Args 1 'zx ds read <dsn>'
      Out-Resp (Invoke-Rpc 'readDataset' @{ dsname = $p[0] }) 'b64'
    }
    'write' {
      Need-Args 2 'zx ds write <dsn> <file>'
      Out-Resp (Invoke-Rpc 'writeDataset' @{ dsname = $p[0]; data = (Get-FileB64 $p[1]) }) 'kv'
    }
    'get' {
      # Default is text (EBCDIC conversion); --binary asks for a byte-faithful read.
      # Leading position only: 'zx ds get --binary ...'.
      $binary = $false
      $ga = @($p)
      if ($ga.Count -ge 1 -and $ga[0] -eq '--binary') {
        $binary = $true
        if ($ga.Count -gt 1) { $ga = @($ga[1..($ga.Count - 1)]) } else { $ga = @() }
      }
      if ($ga.Count -ne 2) { Die 'usage: zx ds get [--binary] <dsn[(member)]> <localdir-or-file>' }
      $dsn = [string]$ga[0]
      $dst = [string]$ga[1]
      if ($dsn -match '\(.*\)') {
        Save-RespB64 (Invoke-Rpc 'readDataset' (New-DsParams $dsn $binary)) (Get-FullPath $dst)
        Say "zx: $dsn -> $dst"
      } else {
        New-Dir $dst
        $dstDir = Get-FullPath $dst
        $lst = Invoke-Rpc 'listDsMembers' @{ dsname = $dsn }
        $err = Get-RpcError $lst
        if ($err) { Die "error: $err" }
        $o = From-Json $lst
        $n = 0
        foreach ($m in @($o.result.items)) {
          $mbr = [string]$m.name
          if (-not $mbr) { continue }
          $target = Get-MemberOutPath $dstDir $mbr
          Save-RespB64 (Invoke-Rpc 'readDataset' (New-DsParams "$dsn($mbr)" $binary)) $target
          $n++
          Say "zx: $dsn($mbr) -> $(Join-Path $dst $mbr)"
        }
        Say "zx: $n member(s)"
      }
    }
    'put' {
      # Default is text (EBCDIC conversion); --binary asks for a byte-faithful write.
      # Leading position only: 'zx ds put --binary ...'.
      $binary = $false
      $pa = @($p)
      if ($pa.Count -ge 1 -and $pa[0] -eq '--binary') {
        $binary = $true
        if ($pa.Count -gt 1) { $pa = @($pa[1..($pa.Count - 1)]) } else { $pa = @() }
      }
      if ($pa.Count -ne 2) { Die 'usage: zx ds put [--binary] <localdir-or-file> <dsn[(member)]>' }
      $src = [string]$pa[0]
      $dsn = [string]$pa[1]
      # Returns an error message, or $null on success. Prints nothing (the caller does).
      function Write-Member([string]$file, [string]$target, [bool]$asBinary) {
        $wp = New-DsParams $target $asBinary
        $wp['data'] = (Get-FileB64 $file)
        return (Get-RpcError (Invoke-Rpc 'writeDataset' $wp))
      }
      if (Test-Path -LiteralPath $src -PathType Leaf) {
        if ($dsn -notmatch '\(.*\)') { Die 'target DSN must include (member) when the source is a file' }
        $err = Write-Member $src $dsn $binary
        if ($err) { Die "$src -> $dsn  FAILED: $err" }
        Say "zx: $src -> $dsn"
      } elseif (Test-Path -LiteralPath $src -PathType Container) {
        if ($dsn -match '\(') { Die 'target DSN must be a bare PDS when the source is a directory' }
        $n = 0
        $fail = 0
        foreach ($f in (Get-ChildItem -LiteralPath $src -File)) {
          $mbr = $f.Name.Split('.')[0].ToUpperInvariant()
          if ($mbr.Length -gt 8) { $mbr = $mbr.Substring(0, 8) }
          $err = Write-Member $f.FullName "$dsn($mbr)" $binary
          if ($err) { Note "$($f.Name) -> $dsn($mbr)  FAILED: $err"; $fail++ }
          else { Say "zx: $($f.Name) -> $dsn($mbr)"; $n++ }
        }
        Say "zx: $n ok, $fail failed"
        if ($fail -gt 0) { exit 1 }
      } else {
        Die "source not found: $src"
      }
    }
    'create' {
      Need-Min 1 "zx ds create <dsn> ['<attrs-json>']"
      $attrs = '{}'
      if ($p.Count -ge 2 -and "$($p[1])".Trim()) { $attrs = [string]$p[1] }
      Out-Resp (Invoke-Rpc 'createDataset' ('{"dsname":' + (To-Json ([string]$p[0])) + ',"attributes":' + $attrs + '}')) 'kv'
    }
    'delete' {
      Need-Args 1 'zx ds delete <dsn>'
      Out-Resp (Invoke-Rpc 'deleteDataset' @{ dsname = $p[0] }) 'kv'
    }
    'copy' {
      Need-Min 2 'zx ds copy <src> <dst> [--ow|-r]'
      Invoke-Cli (@('data-set', 'copy') + $p)
    }
    'rename' {
      Need-Args 2 'zx ds rename <a> <b>'
      Out-Resp (Invoke-Rpc 'renameDataset' @{ dsnameBefore = $p[0]; dsnameAfter = $p[1] }) 'kv'
    }
    default { Die 'zx ds {list|members|read|write|get|put|create|delete|copy|rename}' }
  }
}

'job' {
  switch ($sub) {
    'list' {
      $pj = @{}
      if ($p.Count -ge 1 -and "$($p[0])".Trim()) {
        $pj['owner'] = $p[0]
        if ($p.Count -ge 2 -and "$($p[1])".Trim()) { $pj['prefix'] = $p[1] }
      }
      Out-Resp (Invoke-Rpc 'listJobs' $pj) 'table' @('id:-8', 'name:-8', 'owner:-8', 'status:-8', 'retcode:-10', 'class:-3')
    }
    'submit' {
      Need-Args 1 'zx job submit <file|uss-path|dsn[(member)]>'
      $t = [string]$p[0]
      if (Test-Path -LiteralPath $t -PathType Leaf) {
        Out-Resp (Invoke-Rpc 'submitJcl' @{ jcl = (Get-FileB64 $t) }) 'kv'
      } elseif ($t.StartsWith('/')) {
        Out-Resp (Invoke-Rpc 'submitUss' @{ fspath = $t }) 'kv'
      } elseif ($t -match '[\\/]') {
        Die "local file not found: $t"
      } else {
        Out-Resp (Invoke-Rpc 'submitJob' @{ dsname = $t }) 'kv'
      }
    }
    'status' {
      Need-Args 1 'zx job status <id>'
      Out-Resp (Invoke-Rpc 'getJobStatus' @{ jobId = $p[0] }) 'kv'
    }
    'spools' {
      Need-Args 1 'zx job spools <id>'
      Out-Resp (Invoke-Rpc 'listSpools' @{ jobId = $p[0] }) 'table' @('id:4', 'ddname:-8', 'stepname:-8', 'procstep:-8', 'dsname:-44')
    }
    'spool' {
      Need-Args 2 'zx job spool <id> <n>'
      if ("$($p[1])" -notmatch '^\d+$') { Die 'zx job spool <id> <n> - <n> must be numeric' }
      Out-Resp (Invoke-Rpc 'readSpool' @{ jobId = $p[0]; spoolId = [int]$p[1] }) 'b64'
    }
    'jcl' {
      Need-Args 1 'zx job jcl <id>'
      Out-Resp (Invoke-Rpc 'getJcl' @{ jobId = $p[0] }) 'b64'
    }
    'cancel'  { Need-Args 1 'zx job cancel <id>';  Out-Resp (Invoke-Rpc 'cancelJob'  @{ jobId = $p[0] }) 'kv' }
    'delete'  { Need-Args 1 'zx job delete <id>';  Out-Resp (Invoke-Rpc 'deleteJob'  @{ jobId = $p[0] }) 'kv' }
    'hold'    { Need-Args 1 'zx job hold <id>';    Out-Resp (Invoke-Rpc 'holdJob'    @{ jobId = $p[0] }) 'kv' }
    'release' { Need-Args 1 'zx job release <id>'; Out-Resp (Invoke-Rpc 'releaseJob' @{ jobId = $p[0] }) 'kv' }
    default   { Die 'zx job {list|submit|status|spools|spool|jcl|cancel|delete|hold|release}' }
  }
}

'uss' {
  switch ($sub) {
    'ls' {
      Need-Args 1 'zx uss ls <path>'
      Out-Resp (Invoke-Rpc 'listFiles' @{ fspath = $p[0] }) 'table'
    }
    'get' {
      Need-Min 1 'zx uss get <remote-path> [<local>]'
      $remote = [string]$p[0]
      $localf = if ($p.Count -ge 2) { [string]$p[1] } else { ($remote -split '/')[-1] }
      Load-Cfg
      $batch = "get $(Quote-Sftp $remote) $(Quote-SftpLocal (Get-FullPath $localf))`nbye`n"
      [void](Invoke-Sftp -Target $script:ZHost -Batch $batch)
      Say "zx: $remote -> $localf"
    }
    'put' {
      Need-Args 2 'zx uss put <local> <remote-path>'
      if (-not (Test-Path -LiteralPath $p[0] -PathType Leaf)) { Die "local file not found: $($p[0])" }
      Load-Cfg
      $batch = "put $(Quote-SftpLocal (Get-FullPath ([string]$p[0]))) $(Quote-Sftp ([string]$p[1]))`nbye`n"
      [void](Invoke-Sftp -Target $script:ZHost -Batch $batch)
      Say "zx: $($p[0]) -> $($p[1])"
    }
    'read' {
      Need-Args 1 'zx uss read <path>'
      Out-Resp (Invoke-Rpc 'readFile' @{ fspath = $p[0] }) 'b64'
    }
    'write' {
      Need-Args 2 'zx uss write <path> <file>'
      Out-Resp (Invoke-Rpc 'writeFile' @{ fspath = $p[0]; data = (Get-FileB64 $p[1]) }) 'kv'
    }
    'rm' {
      Need-Args 1 'zx uss rm <path>'
      Out-Resp (Invoke-Rpc 'deleteFile' @{ fspath = $p[0] }) 'kv'
    }
    'mkdir' {
      Need-Args 1 'zx uss mkdir <path>'
      Out-Resp (Invoke-Rpc 'createFile' @{ fspath = $p[0]; isDir = $true }) 'kv'
    }
    'mv' {
      Need-Args 2 'zx uss mv <a> <b>'
      Out-Resp (Invoke-Rpc 'moveFile' @{ source = $p[0]; target = $p[1] }) 'kv'
    }
    'cp' {
      Need-Args 2 'zx uss cp <a> <b>'
      Out-Resp (Invoke-Rpc 'copyUss' @{ srcFsPath = $p[0]; dstFsPath = $p[1] }) 'kv'
    }
    'chmod' {
      Need-Args 2 'zx uss chmod <mode> <path>'
      Out-Resp (Invoke-Rpc 'chmodFile' @{ fspath = $p[1]; mode = $p[0] }) 'kv'
    }
    'chown' {
      Need-Args 2 'zx uss chown <owner> <path>'
      Out-Resp (Invoke-Rpc 'chownFile' @{ fspath = $p[1]; owner = $p[0] }) 'kv'
    }
    'chtag' {
      Need-Args 2 'zx uss chtag <tag> <path>'
      Out-Resp (Invoke-Rpc 'chtagFile' @{ fspath = $p[1]; tag = $p[0] }) 'kv'
    }
    'sh' {
      Need-Min 1 "zx uss sh '<cmd>'"
      Out-Resp (Invoke-Rpc 'unixCommand' @{ commandText = ($p -join ' ') }) 'text'
    }
    default { Die 'zx uss {ls|get|put|read|write|rm|mkdir|mv|cp|chmod|chown|chtag|sh}' }
  }
}

'tso' {
  if ($rest.Count -lt 1) { Die "usage: zx tso '<cmd>'" }
  Out-Resp (Invoke-Rpc 'tsoCommand' @{ commandText = ($rest -join ' ') }) 'text'
}

'system' {
  switch ($sub) {
    'apf'        { Out-Resp (Invoke-Rpc 'listApf' '{}') 'table' @('volume:-6', 'dsname:-44') }
    'linklist'   { Out-Resp (Invoke-Rpc 'listLinklist' '{}') 'table' @('volume:-6', 'dsname:-44', 'apf:-5') }
    'proclib'    { Out-Resp (Invoke-Rpc 'listProclib' '{}') 'table' }
    'syslog'     { Out-Resp (Invoke-Rpc 'viewSyslog' '{}') 'text' }
    'parmlib'    { Invoke-Cli @('system', 'list-parmlib') }
    'subsystems' { Invoke-Cli @('system', 'list-subsystems') }
    'symbol'     { Need-Args 1 'zx system symbol <name>'; Invoke-Cli @('system', 'display-symbol', $p[0]) }
    default      { Die 'zx system {apf|linklist|proclib|parmlib|subsystems|symbol <s>|syslog}' }
  }
}

'tool' {
  switch ($sub) {
    'amblist'  { Invoke-Cli (@('tool', 'amblist')  + $p) }
    'run'      { Invoke-Cli (@('tool', 'run')      + $p) }
    'search'   { Invoke-Cli (@('tool', 'search')   + $p) }
    'dynalloc' { Invoke-Cli (@('tool', 'bpxwdy2')  + $p) }
    'dsect'    { Invoke-Cli (@('tool', 'ccnedsct') + $p) }
    default    { Die "zx tool {amblist <dsn> --cs '<stmts>'|run <pgm> [opts]|search <dsn> <str>|dynalloc '<parm>'|dsect --ad <a> --cd <c>}" }
  }
}

'console' {
  # consoleCommand RPC: the server spawns the APF-authorized zoa binary per
  # request (next to zo, on PATH, or via ZOA_PATH on the remote host)
  $usage = "usage: zx console '<cmd>' [--cn <name>] [--timeout <s>] [--no-wait]"
  if ($rest.Count -lt 1) { Die $usage }
  $cn = ''
  $ct = ''
  $cw = $null
  $words = @()
  $i = 0
  while ($i -lt $rest.Count) {
    $a = [string]$rest[$i]
    if ($a -eq '--cn' -or $a -eq '--console-name') {
      if ($i + 1 -ge $rest.Count) { Die "$a requires a value" }
      $cn = [string]$rest[$i + 1]; $i += 2
    } elseif ($a -eq '--timeout') {
      if ($i + 1 -ge $rest.Count) { Die '--timeout requires a value' }
      $ct = [string]$rest[$i + 1]; $i += 2
    } elseif ($a -eq '--no-wait') {
      $cw = $false; $i += 1
    } else {
      $words += $a; $i += 1
    }
  }
  $cmdText = ($words -join ' ')
  if (-not $cmdText) { Die $usage }
  if ($ct -and ($ct -notmatch '^\d+$')) { Die '--timeout must be a number of seconds' }
  $cp = @{ commandText = $cmdText }
  if ($cn) { $cp['consoleName'] = $cn }
  if ($ct) { $cp['timeout'] = [int]$ct }
  if ($null -ne $cw) { $cp['wait'] = $cw }
  Out-Resp (Invoke-Rpc 'consoleCommand' $cp) 'text'
}

default { Die "unknown group '$grp' - run 'zx help'" }
}
