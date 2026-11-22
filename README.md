# desk-buddy
a desk buddy with cute robo eyes and lyrics sync on youtube and spotift web. with extra features like clock, calendar,spotify now playing card , youtube thumnail and a lot.


Mochi V2.1 — User Manual
ESP32 desk companion: robo eyes, emotions, clock, calendar, live lyrics, Spotify, YouTube thumbnail, stopwatch and Pomodoro
1. What Mochi can do
Mochi is a small desk companion built from an ESP32 and a 128x64 OLED. It shows animated robot eyes that react to its mood, a clock and calendar, live synced lyrics for YouTube and Spotify, a Spotify "now playing" card, the thumbnail of the YouTube video you are watching, a stopwatch and a Pomodoro timer. A web page served by the ESP32 controls everything, and one touch sensor gives you quick control without a phone.
Screen
What it shows
Eyes
Animated eyes with 10 expressions driven by Mochi's mood
Clock
Big digital time, date and a seconds line (flips to the calendar now and then)
Calendar
Month grid with today highlighted
Lyrics
Synced lyrics, written word by word (YouTube and Spotify)
Spotify
Now playing card: title, artist, progress bar
Thumbnail
The current YouTube video thumbnail, dithered for the OLED
Stopwatch
Stopwatch with laps
Pomodoro
Focus / break timer with a full-screen alert
2. Hardware and wiring
Part
Connection
OLED (SSD1306, I2C, address 0x3C)
SDA to GPIO 21, SCL to GPIO 22, VCC to 3V3, GND to GND
TTP223 touch module
VCC to 3V3, GND to GND, I/O (OUT) to D4 (GPIO 4)
TTP223 jumper pads: leave pads A and B open (not soldered). Open gives the default behaviour the sketch expects: the pin goes HIGH while you touch and goes LOW when you let go. Soldering pad B turns on toggle mode, which breaks tap and hold. If you soldered pad A the module is active LOW; then set TOUCH_ACTIVE_HIGH to 0 in the sketch.
The ESP32 must be on a 2.4 GHz Wi-Fi network. Your laptop and phone must be on the same network.
3. Installing the ESP32 sketch
1.  In the Arduino IDE install the ESP32 board package, then install the libraries Adafruit SSD1306 and Adafruit GFX Library (accept any extra libraries it offers).
2.  Put esp32_mochi_v2.ino in a folder named esp32_mochi_v2 and open it.
3.  Edit WIFI_SSID and WIFI_PASS. Check TZ_OFFSET_SEC (19800 = India, UTC+5:30; change it for another time zone, in seconds).
4.  Choose the board ESP32 Dev Module, pick the port and upload.
5.  After boot Mochi shows "Connecting WiFi..." and then, for 6 seconds, its address: http://<IP> and http://mochi.local. Note the IP.
Tip: reserve a fixed IP for the ESP32 in your router so the address never changes.
4. Installing the Chrome extension
The extension feeds Mochi with YouTube lyrics, the YouTube thumbnail and Spotify data. It has four files: manifest.json, background.js, content.js and spotify.js.
1.  Put the four files in one folder.
2.  Set the ESP32 IP in two places: ESP_IP in background.js and the http://<IP>/* line in manifest.json.
3.  Open chrome://extensions, turn on Developer mode, click Load unpacked and choose the folder.
4.  After any change to the files, click the reload icon on the extension and refresh your YouTube or Spotify tabs.
5. Touch sensor
Gesture
What happens
Tap (shorter than 0.7 s)
Switches between eyes and clock. Mochi goes back to Auto mode with that screen as home
Hold (about 0.7 s or more)
Pets Mochi: hearts, more love and happiness
Tap during an alert
Dismisses the alert
After a tap, lyrics and the Spotify card stay out of the way for 15 seconds so you can look at the clock or eyes even while music plays. Your choice of home screen (eyes or clock) is saved and survives a reboot. Tapping very fast makes Mochi annoyed.
6. The web page
Open http://<IP> or http://mochi.local on a phone or laptop on the same Wi-Fi.
Section
Controls
Top line
What is on screen now, which screen you selected, and the home screen
Screen
Auto, Eyes, Clock, Calendar, Lyrics, Spotify, Thumbnail, Stopwatch, Pomodoro
Touch sensor line
Shows TOUCHED or idle plus tap and hold counters (handy to test the wiring)
Emotions
Mood bars, 10 expression buttons, Auto emotions, Pet
Clock
Live time, 24-hour toggle, Show calendar
Stopwatch
Start/Stop, Lap, Reset, lap list
Pomodoro
Start/Pause, Skip, Reset, minutes for focus / short / long break, Save
Now playing
Current Spotify song, the Spotify lyrics ON/OFF switch and the lyrics timing + / − buttons
7. Screens and Auto mode
Auto is the default. Your home screen (eyes or clock, switched by touch) is shown normally. While music plays, Auto automatically shows:
1.  Lyrics, when synced lyric lines are arriving. If no new line arrives for 8 seconds (a long instrumental, or the song stopped) Auto moves on.
2.  The Spotify card, if Spotify is playing and no lyrics are arriving.
3.  Otherwise your home screen.
Picking any other screen on the web page locks Mochi to that screen. Pick Auto to unlock. Starting the stopwatch or Pomodoro from the web page switches to their screens. The Thumbnail screen is manual only.
8. Eyes and the emotion system
Expressions: normal, happy, sad, angry, sleepy, surprised, love (beating hearts), wink, excited (trembling eyes) and bored. Eyes blink, glance around and sway gently when music is playing. Expression changes happen behind a blink.
Moods (shown as bars on the web page):
Mood
Goes up
Goes down
Happy
Music, petting, finishing a focus session
Drifts back to normal; falls if Mochi is very bored
Energy
Daytime, music, a running focus session
Evening and especially night (23:00 to 06:00)
Love
Petting
Fades slowly
Bored
Nothing happening (no music, stopwatch or Pomodoro)
Tapping, petting, music, timers
Annoyed (hidden)
Tapping quickly
Fades within seconds
With Auto emotions on, Mochi picks the next expression from its mood every few seconds: a happy Mochi smiles and winks, a tired one gets sleepy, a petted one shows hearts, a bored one looks bored, and a poked one gets angry. Pressing an expression button sets it by hand, switches to the Eyes screen and turns Auto emotions off. Press Auto emotions to turn it back on. Pet on the web page does the same as holding the touch sensor. Mochi also reacts to events: it gets excited when music starts and when a focus session ends.
9. Clock and calendar
• The time comes from the internet (NTP) over Wi-Fi. Until it is synced the screen says "SYNCING TIME".
• The clock shows a big time with a blinking colon, the date (for example WED 18 NOV) and a thin line along the bottom that fills once per minute.
• 24-hour / 12-hour: use the 24-hour switch on the web page (12-hour mode shows AM or PM in the corner). The setting is saved.
• On the Clock screen Mochi flips to the calendar for 4 seconds in every 30. Choose the Calendar screen to keep it on. Today is highlighted.
• From 23:00 to 06:00 the display dims automatically.
10. Lyrics
YouTube
Open a song on youtube.com or music.youtube.com (a normal watch page). The extension reads the title and playback position, finds synced lyrics online (LRCLIB) and sends each line to Mochi. Mochi "writes" the line word by word in a big font as it is sung. A line that does not fit scrolls up smoothly. Pausing clears the screen. If no synced lyrics exist the screen says "No synced lyrics".
Spotify (option)
Open open.spotify.com (Spotify Web Player) in Chrome and play a song. With Spotify lyrics: ON (the default, switch on the web page under Now playing) Auto mode shows synced lyrics for Spotify songs, written word by word like YouTube. If no synced lyrics are found, the Spotify card simply stays on screen. With the switch OFF, Spotify only shows the card. YouTube lyrics are not affected by this switch.
If YouTube and Spotify play at once, Mochi follows the player that started most recently.
Tuning the timing
Open the Mochi web page and use the Lyrics timing buttons under Now playing. Lyrics too late? Press + next to YouTube or Spotify. Too early? Press −. Each press moves the timing by 0.1 second; it is saved on Mochi and the extension picks it up within a few seconds. YouTube and Spotify each have their own value, and both start at +0.3 s. Spotify only shows whole seconds of the song position, so the extension uses the most exact position it can find and keeps its own clock between updates; that also keeps Spotify lyrics on time when its tab is in the background. The words inside a line are timed by an estimate (longer words get more time), so they are close to the singing but not exact.
Limits
The OLED font is ASCII only, so non-Latin letters are removed. Matching uses the song title and length, so unusual video titles may find nothing.
11. Spotify now playing card
Shows NOW PLAYING with a play or pause icon, the song title (scrolls sideways if long), the artist, a progress bar and elapsed / total time. It needs the Spotify Web Player open in Chrome with the extension. If nothing is received for 20 seconds it shows NOTHING PLAYING. The Spotify desktop app is not supported.
12. YouTube thumbnail
Play a video on YouTube, then choose the Thumbnail screen. The extension converts the thumbnail into a 128x64 black and white picture (dithered) and sends it. It is sent again when the video changes and every 45 seconds. Before any video has played the screen says NO THUMBNAIL YET. To make pictures brighter or darker change THUMB_GAMMA in background.js (lower is brighter).
13. Stopwatch
• Start/Stop starts or pauses it and switches Mochi to the Stopwatch screen.
• Lap records a lap while it is running. The OLED shows the latest lap split and the web page lists up to 6 laps.
• Reset clears time and laps.
• The OLED shows MM:SS.t (tenths of a second), and H:MM:SS after one hour, with the state RUNNING, PAUSED or READY. The stopwatch keeps running if you change screens.
14. Pomodoro
• Default cycle: 25 minutes focus, 5 minutes short break, and a 15 minute long break after every 4th focus session. Change the minutes in the web page and press Save (focus 1 to 180, short 1 to 60, long 1 to 120). They are saved in flash.
• Start/Pause runs the timer and switches to the Pomodoro screen. Skip jumps to the next phase quietly. Reset returns to focus session 1.
• The screen header shows FOCUS 1/4 (and so on), SHORT BREAK or LONG BREAK, with RUNNING or PAUSED and a progress bar.
• When a phase ends the next one starts automatically and the whole screen flashes for 8 seconds with BREAK! or FOCUS!. Tap or hold the touch sensor to dismiss it. The timer keeps running even if you are on another screen.
15. Troubleshooting
Problem
What to do
Touch does nothing
Open the web page and touch the module: the Touch sensor line should say TOUCHED and the tap counter should rise. The Serial Monitor (115200) also prints "touch: tap" and "touch: hold". Check OUT to D4 (GPIO 4), VCC to 3V3, GND to GND. The module's LED should light when touched. Keep pads A and B open. If the LED lights but nothing counts, try TOUCH_ACTIVE_HIGH 0. Do not touch the module while it powers up
OLED blank
Check SDA 21, SCL 22, power and the address (OLED_ADDR 0x3C, sometimes 0x3D)
Garbled or noisy screen
Lower I2C_HZ to 400000
Cannot open the web page
Use the IP shown at boot. Phone and ESP32 must be on the same Wi-Fi (guest networks often block this)
Clock says SYNCING TIME
The ESP32 needs internet access; wait a few seconds
No lyrics on YouTube
Use a normal watch page, check ESP_IP in both extension files, reload the extension and refresh the tab
No lyrics on Spotify
Use open.spotify.com in Chrome, make sure Spotify lyrics is ON, and reload the extension. Some songs have no synced lyrics
Spotify card not updating
The Spotify tab must be open; the extension must be reloaded after edits
Lyrics too early or late
Use the + / − buttons under Lyrics timing on the web page (YouTube and Spotify each have their own)
Thumbnail missing
Play a YouTube video first, then choose the Thumbnail screen
Eyes will not change
Auto emotions may be off, or you are on a locked screen. Choose Auto
16. Settings reference
Sketch (top of esp32_mochi_v2.ino):
Setting
Meaning
WIFI_SSID, WIFI_PASS
Wi-Fi login
TZ_OFFSET_SEC
Time zone in seconds (19800 = UTC+5:30)
OLED_ADDR, SDA_PIN, SCL_PIN, I2C_HZ
Display address, pins and bus speed
TOUCH_PIN, TOUCH_DIGITAL, TOUCH_ACTIVE_HIGH
Touch module pin (4), digital mode (1 for TTP223) and active level
LONG_PRESS_MS
Hold time for petting (700)
FONT_SIZE
Lyric text size (2)
AUTO_RETURN_MS
Seconds without lyrics before Auto leaves the lyrics screen (8000 ms)
CAL_FLIP
1 = clock flips to the calendar every 30 s
MARQUEE_PPS
Scroll speed of long song titles
ALERT_MS
Alert length (8000 ms)
NIGHT_DIM
1 = dim the screen at night
Extension (background.js): ESP_IP and THUMB_GAMMA (thumbnail brightness). Lyric timing is not set in the file: use the + / − buttons on the web page (the file only holds the starting values, 0.3 s for both).
17. Quick reference
I want to...
Do this
Switch eyes and clock
Tap the touch sensor
Pet Mochi
Hold the touch sensor, or press Pet
Show lyrics
Play a song on YouTube or Spotify Web with Auto selected
Turn Spotify lyrics off
Web page, Now playing, Spotify lyrics: OFF
See the video thumbnail
Web page, Screen, Thumbnail
Start a 25 minute focus session
Web page, Pomodoro, Start
Time something
Web page, Stopwatch, Start
Set a mood by hand
Web page, Emotions, tap an expression
Let Mochi decide again
Web page, Emotions, Auto emotions
Go back to normal
Web page, Screen, Auto
Appendix: web addresses
The web page uses these addresses, which you can also open yourself: /api/state, /api/screen?v=eyes|clock|calendar|lyrics|spotify|thumb|stopwatch|pomodoro|auto, /api/expr?e=happy, /api/emotions?on=1, /api/pet, /api/cfg?h24=1&splyrics=1&focus=25&short=5&long=15, /api/sw?a=toggle|lap|reset, /api/pomo?a=toggle|skip|reset. The extension sends data to /lyric, /np and /thumb.
