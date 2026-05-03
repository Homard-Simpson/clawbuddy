# Waveshare ESP32-S3 Touch-AMOLED-1.8 CNC Aluminum Case v0.1

First-pass two-piece CNC case for the Waveshare ESP32-S3 Touch-AMOLED-1.8 board, based on the Tinkercad board STL in `~/Downloads/Waveshare_ ESP32-S3-Touch-AMOLED-1.8 (29957).stl`.

## Files

- `waveshare_amoled_1p8_cnc_bottom_tray_v0_1.stl` — machine from ~14 mm aluminum stock.
- `waveshare_amoled_1p8_cnc_top_bezel_v0_1.stl` — machine from ~4 mm aluminum stock.
- `waveshare_amoled_1p8_cnc_assembly_preview_v0_1.stl` — preview only; includes board reference/screen reference.
- `waveshare_amoled_1p8_cnc_case_v0_1.blend` — editable Blender source.
- `make_case_blender.py` — parametric-ish generator script.

## Basic dimensions

- Outer case: 56 x 50 x 18 mm assembled.
- Bottom tray: 56 x 50 x 14 mm.
- Top bezel: 56 x 50 x 4 mm.
- Board cavity: 42.6 x 35.6 x 12.2 mm.
- Display opening: 31.5 x 36.5 mm.
- Screw pattern: 46 x 41 mm, four M2.5 screws.

## CNC notes

- Designed for 3-axis CNC with no undercuts.
- Bottom tray has a top-side pocket and an open edge USB-C notch.
- Top bezel has through screw holes and counterbores.
- Suggested material: 6061 aluminum.
- Use small edge chamfers/deburring after machining.
- Use Kapton/fishpaper/printed insulating liner between PCB and aluminum. Do **not** let the PCB touch bare aluminum.

## Hardware

- 4x M2.5 screws, likely 8–10 mm depending actual threading/counterbore depth.
- Bottom holes are modeled as M2.5 tap-drill blind holes; confirm tap/drill workflow in CAM.

## Important caveat

This is v0.1 from the downloaded board model only. Before cutting aluminum, print in plastic or machine foam/wax first and test:

1. board fit in tray
2. USB-C clearance
3. screen/window alignment
4. screw length and hole alignment
5. Wi-Fi/Bluetooth performance inside aluminum

Aluminum may weaken wireless range. If this becomes the ClawBuddy production direction, add a plastic/rubber antenna window or leave the antenna side exposed.

## v0.2 update

- Increased outer corner radius to 6 mm.
- Added stronger rounded edge breaks: ~1.0 mm on bottom tray, ~0.8 mm on top bezel.
- v0.2 files are preferred over v0.1 for CNC because they avoid sharp outside corners/edges.
