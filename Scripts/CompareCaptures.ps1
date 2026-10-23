# Compares RenderCheck captures: how much of the image each render path changes against the
# hidden-model baseline, how closely the mesh-shader and fallback paths agree, and whether the
# occluding cube's region (the left quarter of the frame) is unaffected by the model.
param([string]$Dir = "$PSScriptRoot\..\Saved\RenderCheck")
Add-Type -AssemblyName System.Drawing

function Read-Pixels([string]$Path) {
	$bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Path))
	$rect = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
	$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$bytes = New-Object byte[] ($data.Stride * $bmp.Height)
	[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
	$result = @{ Width = $bmp.Width; Height = $bmp.Height; Stride = $data.Stride; Bytes = $bytes }
	$bmp.UnlockBits($data); $bmp.Dispose()
	$result
}

# Fraction of pixels whose largest channel difference exceeds Threshold, optionally within an x range.
function Get-ChangedFraction($A, $B, [int]$Threshold = 12, [double]$X0 = 0, [double]$X1 = 1) {
	$changed = 0; $total = 0
	$xa = [int]($A.Width * $X0); $xb = [int]($A.Width * $X1)
	for ($y = 0; $y -lt $A.Height; $y += 2) {
		$row = $y * $A.Stride
		for ($x = $xa; $x -lt $xb; $x += 2) {
			$i = $row + $x * 4
			$d = [Math]::Max([Math]::Abs($A.Bytes[$i] - $B.Bytes[$i]), [Math]::Max([Math]::Abs($A.Bytes[$i + 1] - $B.Bytes[$i + 1]), [Math]::Abs($A.Bytes[$i + 2] - $B.Bytes[$i + 2])))
			if ($d -gt $Threshold) { $changed++ }
			$total++
		}
	}
	[Math]::Round(100.0 * $changed / [Math]::Max($total, 1), 2)
}

foreach ($hidden in Get-ChildItem $Dir -Filter "*_hidden.png") {
	$name = $hidden.Name -replace "_hidden\.png$", ""
	$base = Read-Pixels $hidden.FullName
	$mesh = Read-Pixels (Join-Path $Dir "${name}_mesh.png")
	$fallback = Read-Pixels (Join-Path $Dir "${name}_fallback.png")
	[pscustomobject]@{
		Sample                    = $name
		"MeshVsBaseline%"         = Get-ChangedFraction $mesh $base
		"FallbackVsBaseline%"     = Get-ChangedFraction $fallback $base
		"MeshVsFallback%"         = Get-ChangedFraction $mesh $fallback
		"CubeRegionChanged%"      = Get-ChangedFraction $mesh $base -X0 0.05 -X1 0.25
	}
}
