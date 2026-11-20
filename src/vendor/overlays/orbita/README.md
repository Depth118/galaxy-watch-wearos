Fixes the doubled ring numbers on the default Pixel face (Orbita, in
ClockworkWatchFacesGoogle). NumberWheel takes `concentric_numbers.png`
(drawable-xhdpi, 32x240 = 12 rows of 20 px) via BitmapFactory, so it is
density-scaled, and computes the row height as `getHeight() / 12` in integer
math. At our 300 dpi the strip becomes 225 px: rows are 18.75 px but drawn
as 18, so row i is i*0.75 px too high and later numbers show the bottom of the
previous one. This overlay replaces the strip with a 128x960 version (rows of
80 px), which scales to a multiple of 12 at 240/280/300/320 dpi.
