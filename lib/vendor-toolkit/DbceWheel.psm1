<#
.SYNOPSIS
    Shared PowerShell helpers for wheel-mod installers and setup wizards.

.DESCRIPTION
    The pieces every installer in the family had rewritten (milestone-wheel-tools
    had Steam discovery three times in one repo): finding Steam libraries and a
    game through the registry and libraryfolders.vdf, enumerating DirectInput
    controllers from PowerShell (with the two COM marshalling workarounds), and
    editing a game INI without destroying its CRLF line endings.

    Import-Module .\DbceWheel.psm1
    Find-SteamGame -AppId 550320
    Get-DirectInputDevices | Format-Table
    Set-IniValueCrlf -Path $ini -Section '/Wheel.Config/0006346e' -Key 'Axis1' -Value 'X&1.0&0.0'

    Lineage: milestone-wheel-tools Install.ps1 / Uninstall.ps1 / WheelSetup.ps1,
    iracing-arcade-wheel tools\installer\install.ps1, 2026-08/09.
#>

function Get-SteamLibraries {
    <#
    .SYNOPSIS  Every Steam library folder on this machine.
    .DESCRIPTION
        Steam is frequently NOT under Program Files; the registry is the only
        reliable source. libraryfolders.vdf then lists every additional library.
    #>
    [CmdletBinding()] param()
    $bases = @()
    foreach ($k in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        $v = Get-ItemProperty $k -ErrorAction SilentlyContinue
        foreach ($name in 'SteamPath', 'InstallPath') { if ($v -and $v.$name) { $bases += ($v.$name -replace '/', '\') } }
    }
    $bases += "${env:ProgramFiles(x86)}\Steam"
    $bases += "$env:ProgramFiles\Steam"
    $libs = @()
    foreach ($base in ($bases | Sort-Object -Unique)) {
        $vdf = Join-Path $base 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            $libs += $base
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) { $libs += ($m.Groups[1].Value -replace '\\\\', '\') }
        }
    }
    $libs | Sort-Object -Unique
}

function Find-SteamGame {
    <#
    .SYNOPSIS  Locates an installed Steam game by app id or by its steamapps\common folder name.
    .OUTPUTS   [pscustomobject] Path, AppId, Name, Library — or nothing.
    #>
    [CmdletBinding()] param([int]$AppId, [string]$FolderName)
    foreach ($lib in Get-SteamLibraries) {
        if ($AppId) {
            $acf = Join-Path $lib "steamapps\appmanifest_$AppId.acf"
            if (Test-Path $acf) {
                $raw = Get-Content $acf -Raw
                $dir = [regex]::Match($raw, '"installdir"\s+"([^"]+)"').Groups[1].Value
                $name = [regex]::Match($raw, '"name"\s+"([^"]+)"').Groups[1].Value
                $path = Join-Path $lib "steamapps\common\$dir"
                if (Test-Path $path) { return [pscustomobject]@{ Path = $path; AppId = $AppId; Name = $name; Library = $lib } }
            }
        }
        if ($FolderName) {
            $path = Join-Path $lib "steamapps\common\$FolderName"
            if (Test-Path $path) { return [pscustomobject]@{ Path = $path; AppId = $AppId; Name = $FolderName; Library = $lib } }
        }
    }
}

function Get-DirectInputDevices {
    <#
    .SYNOPSIS  Enumerates attached DirectInput game controllers without a native helper.
    .DESCRIPTION
        Two workarounds are load-bearing: PowerShell 7 / .NET Core cannot marshal a
        delegate through a COM interface, so the enum callback is passed as a raw
        function pointer; and DirectInput8Create needs a real module handle from
        GetModuleHandleW(IntPtr.Zero) — a null string marshals to E_INVALIDARG.
        Key is guidProduct.Data1 = (PID << 16) | VID, the value games use as a
        config key (MOZA R12 = 0006346e). Type 0x18 is DirectInput's SIXDOF/1STPERSON
        class, which is what any base with 6+ axes gets — never DRIVING.
    .OUTPUTS   Key, Vid, Pid, Name, Type, ForceFeedback, Virtual
    #>
    [CmdletBinding()] param()
    if (-not ('DbceDIScan' -as [type])) {
        Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Collections.Generic;
[StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
public struct DbceDIDEVICEINSTANCEW {
  public int dwSize; public Guid guidInstance; public Guid guidProduct; public int dwDevType;
  [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string tszInstanceName;
  [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string tszProductName;
  public Guid guidFFDriver; public ushort wUsagePage; public ushort wUsage;
}
[ComImport, Guid("BF798031-483A-4DA2-AA99-5D64ED369700"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface DbceIDirectInput8W {
  int CreateDevice(ref Guid g, out IntPtr d, IntPtr u);
  int EnumDevices(int t, IntPtr cb, IntPtr r, int f);
  int GetDeviceStatus(ref Guid g); int RunControlPanel(IntPtr h, int f); int Initialize(IntPtr h, int v);
}
public static class DbceDIScan {
  public delegate int Cb(ref DbceDIDEVICEINSTANCEW i, IntPtr p);
  [DllImport("dinput8.dll")] static extern int DirectInput8Create(IntPtr h,int v,ref Guid r,out DbceIDirectInput8W p,IntPtr u);
  [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandleW(IntPtr n);
  static Cb _keep; public static List<string> Items = new List<string>();
  static int OnDevice(ref DbceDIDEVICEINSTANCEW i, IntPtr p) {
    uint key = BitConverter.ToUInt32(i.guidProduct.ToByteArray(), 0);
    Items.Add(string.Format("{0:x8}|{1}|{2}|{3}", key, i.tszProductName, i.dwDevType & 0xFF, (i.dwDevType & 0x10000) != 0));
    return 1;
  }
  public static void Run() {
    Items.Clear();
    var iid = new Guid("BF798031-483A-4DA2-AA99-5D64ED369700"); DbceIDirectInput8W di;
    if (DirectInput8Create(GetModuleHandleW(IntPtr.Zero), 0x0800, ref iid, out di, IntPtr.Zero) != 0) return;
    _keep = new Cb(OnDevice);
    di.EnumDevices(4, Marshal.GetFunctionPointerForDelegate(_keep), IntPtr.Zero, 1);  // DI8DEVCLASS_GAMECTRL, attached
  }
}
'@
    }
    [DbceDIScan]::Run()
    [DbceDIScan]::Items | ForEach-Object {
        $f = $_ -split '\|'
        $key = [Convert]::ToUInt32($f[0], 16)
        [pscustomobject]@{
            Key = $f[0]; Vid = ('{0:x4}' -f ($key -band 0xFFFF)); Pid = ('{0:x4}' -f ($key -shr 16))
            Name = $f[1]; Type = [int]$f[2]; ForceFeedback = [bool]::Parse($f[3])
            Virtual = [bool]($f[1] -match 'vjoy|vigem|xoutput|vxbox|vgamepad|emulated|virtual')
        }
    }
}

function Set-IniValueCrlf {
    <#
    .SYNOPSIS  Sets key=value inside an INI section without touching the file's line endings.
    .DESCRIPTION
        A ".*?$" regex eats the CR and silently converts edited lines to bare LF,
        which some games then refuse. Values are matched with [^\r\n]* instead,
        and the number of bare LFs is checked after writing. The section is
        created (with the file's own newline) if absent.
    #>
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Section,
        [Parameter(Mandatory)] [string]$Key,
        # An empty value is legitimate and often meaningful: OutRun 2006 treats
        # an empty DeviceGuid as "this input slot is off". Without
        # AllowEmptyString the cmdlet refuses to write the very value that
        # disables a feature.
        [Parameter(Mandatory)] [AllowEmptyString()] [string]$Value
    )
    $raw = Get-Content $Path -Raw
    $nl = if ($raw -match "`r`n") { "`r`n" } else { "`n" }
    $endedWithNewline = $raw.EndsWith($nl)
    # \z, not \Z: .NET's \Z also matches BEFORE a trailing newline, so on the
    # file's last section the match ended mid-CRLF, and appending a key left a
    # bare LF behind - the exact corruption this function exists to prevent.
    $secRx = "(?s)(\[" + [regex]::Escape($Section) + "\].*?)(?=" + [regex]::Escape($nl) + "\[|\z)"
    $m = [regex]::Match($raw, $secRx)
    if (-not $m.Success) {
        $raw = $raw.TrimEnd("`r", "`n") + $nl + $nl + "[$Section]" + $nl + "$Key=$Value" + $nl
    } else {
        $block = $m.Groups[1].Value
        # "key = value" is as common as "key=value" - OutRun 2006 writes the
        # first, Milestone the second. Matching only the tight form appends a
        # SECOND line instead of replacing, leaving a duplicate key whose
        # winner depends on the parser. Capture the indent and the separator
        # so the file's own spacing survives the edit.
        $keyRx = "(?m)^([ \t]*)" + [regex]::Escape($Key) + "([ \t]*=[ \t]*)[^\r\n]*"
        $existing = [regex]::Match($block, $keyRx)
        if ($existing.Success) {
            $block = $block.Remove($existing.Index, $existing.Length).Insert(
                $existing.Index, $existing.Groups[1].Value + $Key + $existing.Groups[2].Value + $Value)
        } else {
            # A new key copies the spacing of whatever key the section already
            # has, so an appended line does not look foreign.
            $sep = "="
            $sib = [regex]::Match($block, "(?m)^[ \t]*[^\[\r\n;][^\r\n=]*?([ \t]*=[ \t]*)")
            if ($sib.Success) { $sep = $sib.Groups[1].Value }
            $block = $block.TrimEnd("`r", "`n") + $nl + "$Key$sep$Value"
        }
        $raw = $raw.Substring(0, $m.Index) + $block + $raw.Substring($m.Index + $m.Length)
    }
    # A key appended to the file's last section would otherwise drop the
    # trailing newline the file arrived with.
    if ($endedWithNewline -and -not $raw.EndsWith($nl)) { $raw += $nl }
    [IO.File]::WriteAllText($Path, $raw)
    if ($nl -eq "`r`n") {
        $bare = ([regex]::Matches($raw, "(?<!`r)`n")).Count
        if ($bare -gt 0) { Write-Warning "$Path now has $bare bare LF line ending(s)" }
    }
}

function Test-GameRunning {
    <# .SYNOPSIS  True if a process with this name (no extension) is running. Installers must refuse while the game runs. #>
    [CmdletBinding()] param([Parameter(Mandatory)] [string]$ProcessName)
    [bool](Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
}

Export-ModuleMember -Function Get-SteamLibraries, Find-SteamGame, Get-DirectInputDevices, Set-IniValueCrlf, Test-GameRunning
