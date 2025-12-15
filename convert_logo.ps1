
# Script to convert JPG to LVGL 1-bit C file using .NET System.Drawing

$sourceFile = "src\AIPLAN_LOGO_FINAL_2020 (1).jpg"
$destFile = "src\logo_aiplan.c"
$targetWidth = 200
# Calculate height to keep aspect ratio
# But we can also cap it. Let's start by loading.

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $sourceFile)) {
    Write-Error "Source file not found: $sourceFile"
    exit 1
}

$bmp = [System.Drawing.Bitmap]::FromFile((Get-Item $sourceFile).FullName)
$ratio = $bmp.Height / $bmp.Width
$targetHeight = [int]($targetWidth * $ratio)

Write-Host "Original Size: $($bmp.Width)x$($bmp.Height)"
Write-Host "Target Size: ${targetWidth}x${targetHeight}"

# Resize
$newBmp = new-object System.Drawing.Bitmap $targetWidth, $targetHeight
$graph = [System.Drawing.Graphics]::FromImage($newBmp)
$graph.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graph.DrawImage($bmp, 0, 0, $targetWidth, $targetHeight)
$graph.Dispose()

# Convert to 1-bit Map (Row-major, MSB first)
# Logic: Darker pixels = 1 (Color), Lighter = 0 (Transparent)
$threshold = 200 # 0-255 brightness
$hexData = @()

$strBuilder = [System.Text.StringBuilder]::new()

$totalBytes = 0

for ($y = 0; $y -lt $targetHeight; $y++) {
    $currentByte = 0
    $bitCount = 0
    
    for ($x = 0; $x -lt $targetWidth; $x++) {
        $color = $newBmp.GetPixel($x, $y)
        # Brightness (Simple average)
        $brightness = ($color.R + $color.G + $color.B) / 3
        
        # If Dark (Text), Bit = 1. If Light (Bg), Bit = 0.
        $bit = if ($brightness -lt $threshold) { 1 } else { 0 }
        
        # MSB First packing
        # Shift current byte left
        # Actually standard is: Byte starts 0.
        # Bit 0 goes to 0x80, Bit 1 to 0x40...
        # So we construct 'currentByte'
        
        if ($bit -eq 1) {
            $shift = 7 - $bitCount
            $currentByte = $currentByte -bor ([int][math]::Pow(2, $shift))
        }
        
        $bitCount++
        
        if ($bitCount -eq 8) {
            # Flush byte
            $strBuilder.Append("0x$($currentByte.ToString('x2')), ")
            $totalBytes++
            $currentByte = 0
            $bitCount = 0
        }
    }
    
    # Flush remaining bits in row
    if ($bitCount -gt 0) {
        $strBuilder.Append("0x$($currentByte.ToString('x2')), ")
        $totalBytes++
    }
    
    $strBuilder.AppendLine("")
}

$bmp.Dispose()
$newBmp.Dispose()

# Generate C File Content
$cContent = @"
#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_AIPLAN_LOGO_FINAL_2020
#define LV_ATTRIBUTE_IMG_AIPLAN_LOGO_FINAL_2020
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_AIPLAN_LOGO_FINAL_2020 uint8_t AIPLAN_LOGO_FINAL_2020_map[] = {
$($strBuilder.ToString())
};

const lv_img_dsc_t AIPLAN_LOGO_FINAL_2020 = {
  .header.cf = LV_IMG_CF_ALPHA_1BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = $targetWidth,
  .header.h = $targetHeight,
  .data_size = $totalBytes,
  .data = AIPLAN_LOGO_FINAL_2020_map,
};
"@

Set-Content -Path $destFile -Value $cContent -Encoding Ascii
Write-Host "Success! Wrote $destFile with $totalBytes bytes."
