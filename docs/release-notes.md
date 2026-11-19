Graphing Calculator {{VERSION}}: a high-school graphing calculator for the **Raspberry Pi 3 B+**. It runs with no operating system and has no network, Wi-Fi or Bluetooth support.

Built from commit {{COMMIT}}.

## New SD card (the calculator only)

1. Download **`graphcalc-{{VERSION}}.img.xz`**.
2. Flash it to a microSD card with [Raspberry Pi Imager](https://www.raspberrypi.com/software/): *Choose OS → Use custom*, pick the file, then choose your card. Flashing erases the card.
3. Put the card in the Pi 3 B+. Connect an HDMI screen and a USB keyboard, then power on.

Or format a card as FAT32 and copy the files from **`graphcalc-{{VERSION}}-sdcard-files.zip`** onto it.

## With the boot menu (pi-launcher)

Copy **`kernel8.img`** onto the menu's card as **`apps/graphcalc.img`**. The menu lists it if `apps/apps.txt` has the line `app=graphcalc.img|Graphing Calculator|graph`. **F10** goes back to the menu.

## Update an existing card (keeps functions and work)

Copy the new **`kernel8.img`** onto the card, replacing the old one (or `apps/graphcalc.img` on a menu card). Your functions, settings and recent calculations are in `graphcalc/state.txt` and are kept.

## Using it

| Key | Screen |
|---|---|
| **F1** | Calculate: type a calculation and press Enter. `a = 7` keeps a value in a letter; `y1 = x^2` makes a function. |
| **F2** | Functions y1 to y6. Tab shows or hides one on the graph. |
| **F3** | Graph. Arrows trace, + and − zoom, **R** zero, **N** minimum, **M** maximum, **I** where two graphs meet, **G** go to an x value, **F** fit to the screen. |
| **F4** | Table of values. |
| **F5** | Settings: the graph window, the table, degrees or radians. |
| **F6** | Help: everything you can type. |
| **F7** | Switch between degrees and radians. |

## If something doesn't work

- **Press F12** on any screen to open the system log. Take a photo of it.
- **`kernel8-debug.img`** logs extra USB detail. Copy it onto the card and rename it to `kernel8.img`.

## Files

| File | What it is |
|---|---|
| `graphcalc-{{VERSION}}.img.xz` | Complete SD card image |
| `graphcalc-{{VERSION}}-sdcard-files.zip` | The same files, to copy onto a FAT32 card |
| `kernel8.img` | The program only, for updating a card or for the boot menu |
| `kernel8-debug.img` | The program with extra USB logging, for troubleshooting |
| `SHA256SUMS.txt` | Checksums of the files above |
