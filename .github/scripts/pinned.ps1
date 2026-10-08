# ci's tools, each one download pinned by its sha256: fetches one into the runner's temp folder and points
# the job's later steps at it (GITHUB_ENV).
#
#   pinned.ps1 clang-format   CLANG_FORMAT: clang-format 22.1.3, out of the windows wheel pip would install
#   pinned.ps1 clang-tidy     CLANG_TIDY: clang-tidy 22.1.8, the same way (pypi has no 22.1.3 of it)
#   pinned.ps1 jdk            JAVA_HOME: temurin 25.0.4.1+1

param([Parameter(Mandatory)][ValidateSet("clang-format", "clang-tidy", "jdk")][string]$Tool)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$pins = @{
	"clang-format" = @{
		Url    = "https://files.pythonhosted.org/packages/86/07/e5a31c0865e1e10d591c4e511e0db79873c0cf48bb62c3934543295b6f5a/clang_format-22.1.3-py2.py3-none-win_amd64.whl"
		Sha256 = "426453233bea583775542da9b859de6e088bf32b4f393527fc5b05bbc7d33ae3"
		File   = "clang-format.exe"
		Env    = "CLANG_FORMAT"
	}
	"clang-tidy"   = @{
		Url    = "https://files.pythonhosted.org/packages/54/af/0580f6145d8a0c218844208a6c90ce539a4bdf65a66b672e6b4604a81c0a/clang_tidy-22.1.8-py2.py3-none-win_amd64.whl"
		Sha256 = "df9bf841ecbf501d08b6fa34523be840f59b1a76d16b6ee5edb0e5c7c22b59c2"
		File   = "clang-tidy.exe"
		Env    = "CLANG_TIDY"
	}
	jdk = @{
		Url    = "https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.4.1%2B1/OpenJDK25U-jdk_x64_windows_hotspot_25.0.4.1_1.zip"
		Sha256 = "00c847d804f4a78e9f04f2683faf14fed898535b177b7fc704486cb0284e9283"
		File   = "javac.exe"
		Env    = "JAVA_HOME"
	}
}

$pin = $pins[$Tool]
$root = Join-Path $env:RUNNER_TEMP "pinned\$Tool"
# a wheel is a zip; expand-archive only takes the name
$archive = Join-Path $env:RUNNER_TEMP "pinned\$Tool.zip"
New-Item -ItemType Directory -Force $root | Out-Null
Invoke-WebRequest -Uri $pin.Url -OutFile $archive -UseBasicParsing
$actual = (Get-FileHash $archive -Algorithm SHA256).Hash
if ($actual -ne $pin.Sha256.ToUpper()) { throw "$($pin.Url) isn't the pinned download (sha256 $actual)" }
Expand-Archive $archive $root -Force

$file = Get-ChildItem $root -Recurse -Filter $pin.File | Select-Object -First 1
if (-not $file) { throw "no $($pin.File) in $($pin.Url)" }
# the jdk's home is the folder over bin\
$value = if ($Tool -eq "jdk") { Split-Path -Parent $file.DirectoryName } else { $file.FullName }
Add-Content -Path $env:GITHUB_ENV -Value "$($pin.Env)=$value" -Encoding ascii
Write-Host "${Tool}: $value"
