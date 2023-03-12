# This script serves:
# - Download and unzip large contents.
# - Download and build third-party libraries.

#
# Script arguments
#
Param (
	[Switch]$skipdownload,
	[Switch]$skipbuild
)

# For test
$dry_run = $false

$should_download = !($PSBoundParameters.ContainsKey('skipdownload'))

$zip_list  = @(
	# Format: (url, zip_dir, zip_filename, unzip_dir)
	<#
	@(
		'https://github.com/NVIDIAGameWorks/SpatiotemporalBlueNoiseSDK/raw/main/STBN.zip',
		'contents',
		'STBN.zip',
		'contents/NVidiaSpatioTemporalBlueNoise'
	),
	@(
		'https://casual-effects.com/g3d/data10/research/model/breakfast_room/breakfast_room.zip',
		'contents',
		'breakfast_room.zip',
		'contents/breakfast_room'
	)
	#>
	@(
		'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2207/dxc_2022_07_18.zip',
		'external',
		'dxc_2022_07_18.zip',
		'external/dxc'
	),
	@(
		'https://github.com/ocornut/imgui/archive/refs/tags/v1.89.3.zip',
		'external',
		'dear_imgui_v1.89.3.zip',
		'external/dear_imgui'
	),
	@(
		'http://www.humus.name/Textures/Footballfield.zip',
		'external',
		'skybox_Footballfield.zip',
		'external/skybox_Footballfield'
	),
	@(
		'http://www.humus.name/Textures/Meadow.zip',
		'external',
		'skybox_Meadow.zip',
		'external/skybox_Meadow'
	),
	@(
		'http://www.humus.name/Textures/IceRiver.zip',
		'external',
		'skybox_IceRiver.zip',
		'external/skybox_IceRiver'
	)
)

#
# Utilities
#
function Not-A-Drill {
	return $dry_run -eq $false
}
function Download-URL {
	Param ($webclient, $target_url, $target_path)
	if (Test-Path $target_path -PathType Leaf) {
		Write-Host "   ", $target_path, "already exists, will be skipped." -ForegroundColor Green
	} else {
		if (Not-A-Drill) {
			#Write-Host "url: ", $target_url -ForegroundColor Green
			#Write-Host "target: ", $target_path -ForegroundColor Green
			#Write-Host "Downloading..." -ForegroundColor Green
			$webclient.DownloadFile($target_url, $target_path)
			#Write-Host "Download finished."
		}
	}
}
function Clear-Directory {
	Param ($dir)
	Write-Host "Clear directory:", $dir -ForegroundColor Green
	if (Not-A-Drill) {
		if (Test-Path $dir) {
			Remove-Item -Recurse -Force $dir
		}
		New-Item -ItemType Directory -Force -Path $dir
	}
}
function Ensure-Subdirectory {
	Param ($dir)
	Write-Host "Ensure directory:", $dir
	if (Not-A-Drill) {
		New-Item -ItemType Directory -Path $dir -Force | Out-Null
	}
}
function Unzip {
	Param ($zip_filepath, $unzip_dir)
	Write-Host "    Unzip to:", $unzip_dir
	if (Not-A-Drill) {
		if (Test-Path $unzip_dir) {
			Remove-Item -Recurse -Force $unzip_dir
		}
		Expand-Archive -Path $zip_filepath -DestinationPath $unzip_dir
	}
}

#
# Download contents
#
$num_zip_files = $zip_list.length
if ($should_download) {
	Write-Host "Download zip files... (count=$num_zip_files)" -ForegroundColor Green
	$webclient = New-Object System.Net.WebClient
	
	foreach ($desc in $zip_list) {
		$content_url = $desc[0]
		$content_zip_dir = $desc[1]
		$content_zip_name = $desc[2]
		$content_unzip_dir = $desc[3]
		
		Ensure-Subdirectory "$pwd/$content_zip_dir/"
		Write-Host ">" $content_zip_name "(" $content_url ")" #-ForegroundColor Green
		
		$zip_path = "$pwd/$content_zip_dir/$content_zip_name"
		$unzip_dir = "$pwd/$content_unzip_dir"
		Download-URL $webclient $content_url $zip_path
		Write-Host "Downloaded: " $zip_path
		Unzip $zip_path $unzip_dir
	}
	
	# TODO: How to close connection
	#$webclient.Close()
} else {
	Write-Host "Skip download due to -skipdownload" -ForegroundColor Green
}

#
# Copy DXC binaries to bin/
#
Ensure-Subdirectory "$pwd/bin/Debug"
Ensure-Subdirectory "$pwd/bin/Release"
Copy-Item -Path "$pwd/external/dxc/bin/x64/dxcompiler.dll" -Destination "$pwd/bin/Debug"
Copy-Item -Path "$pwd/external/dxc/bin/x64/dxcompiler.dll" -Destination "$pwd/bin/Release"
Copy-Item -Path "$pwd/external/dxc/bin/x64/dxil.dll" -Destination "$pwd/bin/Debug"
Copy-Item -Path "$pwd/external/dxc/bin/x64/dxil.dll" -Destination "$pwd/bin/Release"