# Swiss Chronograph

## English

### One swipe. That’s it.

Are you tired of timer apps where your finger covers exactly what you are
trying to set? Or of tapping through menus again and again just to start a
simple countdown?

**Swiss Chronograph is different.** Swipe once along the vertical ruler, choose
your time, and you are done. The interface was designed to be as minimal as
possible: as little visual clutter as possible and as few clicks as possible.

The timer can be minimised and continues running in the background. When the
countdown finishes, the display alternates between light and dark once per
second while the digits invert with it. The alarm plays a proper 8-bit sound:
10 seconds of sound followed by a 2-second pause, repeated until the alarm is
dismissed or stops automatically.

Choose a permanent Light or Dark theme, or use Shake mode to switch between
them with a movement of your wrist.

Touch is the fastest way to use Swiss Chronograph, but every essential action
is also available through the hardware buttons. That means the timer remains
fully usable when the screen is wet, your hands are dirty, or you are wearing
gloves.

And yes, it is genuinely Swiss: it was designed and developed in Switzerland
by a Swiss developer.

The project is open source. Have fun with it!

**Greetings,  
Maru**

### Features

- Made exclusively for **Pebble Time 2**
- Pebble SDK platform: **`emery`**
- Vertical touch ruler from 0 to 180 minutes
- Stable one-minute detents
- One-swipe selection and automatic start animation
- Four centred animated chevrons
- Large custom PPF pixel digits
- Quiet zero position without a large `0`
- `MM:SS` countdown
- Optional hundredths below 100 minutes
- Pause and resume
- Add one minute while paused
- Delete the timer while paused
- Minimise a running or paused timer
- Persistent timer state and wakeup alarm
- Light, Dark and Shake themes
- Independently configurable read-line colour
- Configurable vibration pattern and alarm volume
- Black/white alarm-screen inversion once per second
- 8-bit alarm loop with 10 seconds of sound and 2 seconds of silence
- Complete hardware-button operation
- Open source under GPL-3.0

### Controls

#### Selecting a duration

- Swipe the vertical ruler to select whole minutes.
- Press Up or Down for one-minute adjustments.
- Hold Up or Down for continuous adjustment.
- Release the ruler to start through the chevron animation.
- Select also starts the chosen duration.

#### While the timer is running

- **Back / top-left control:** minimise
- **Select / middle-right control:** pause or resume
- **Up / top-right control while paused:** add one minute
- **Down / bottom-right control while paused:** delete the timer
- **Select during the alarm:** dismiss the alarm and return to the ruler

### Compatibility

Swiss Chronograph is built, tested and supported **only for Pebble Time 2**:

```json
"targetPlatforms": ["emery"]
```

No other Pebble platform is supported.

### Building

Requirements:

- Pebble SDK 4.9 or newer
- Node.js and npm

```bash
npm install
pebble clean
pebble build
```

Install through the Pebble developer connection:

```bash
pebble install --phone <watch-ip>
```

### Origin and licence

Swiss Chronograph is based on **Instant Timer** by Andrew Howe and
was extensively redesigned by Maru Kitano.

The project is licensed under the
[GNU General Public License version 3](LICENSE).

The original-project copyright notice and the modifications are documented in
[NOTICE.md](NOTICE.md). The bundled alarm sound and its separate licence are
documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## Deutsch

### Ein Swipe. Das war’s.

Bist du es auch leid, dass du bei Timer-Apps mit dem Finger genau das
verdeckst, was du einstellen möchtest? Oder dass du dich durch Menüs klicken
musst, nur um einen einfachen Countdown zu starten?

**Swiss Chronograph ist anders.** Ein Swipe über das vertikale Lineal, Zeit
auswählen, fertig. Die Oberfläche wurde so minimalistisch wie möglich
gestaltet: so wenig visuelle Ablenkung und so wenige Klicks wie möglich.

Der Timer kann minimiert werden und läuft im Hintergrund weiter. Wenn der
Countdown endet, wechselt der gesamte Bildschirm einmal pro Sekunde zwischen
hell und dunkel, während sich die Ziffern passend dazu umkehren. Dazu spielt
ein richtiger 8-Bit-Alarmton: 10 Sekunden Ton, danach 2 Sekunden Pause, bis der
Alarm beendet wird oder automatisch stoppt.

Du kannst dauerhaft ein helles oder dunkles Theme verwenden. Im Shake-Modus
wechselst du mit einer Bewegung des Handgelenks zwischen beiden.

Am schnellsten funktioniert Swiss Chronograph über Touch. Trotzdem ist jede
wichtige Funktion auch vollständig über die Hardwaretasten bedienbar. Damit
kannst du den Timer auch verwenden, wenn das Display nass ist, deine Hände
schmutzig sind oder du Handschuhe trägst.

Und ja, er ist tatsächlich schweizerisch: Er wurde in der Schweiz von einem
Schweizer entwickelt.

Das Projekt ist Open Source. Viel Spass damit!

**Gruss,  
Maru**

### Funktionen

- Ausschliesslich für die **Pebble Time 2**
- Pebble-SDK-Plattform: **`emery`**
- Vertikales Touch-Lineal von 0 bis 180 Minuten
- Stabile Ein-Minuten-Rastung
- Auswahl mit einem Swipe und automatische Startanimation
- Vier zentrierte animierte V-Pfeile
- Grosse eigene PPF-Pixelziffern
- Ruhiger Nullzustand ohne grosse `0`
- `MM:SS`-Countdown
- Optionale Hundertstelsekunden unter 100 Minuten
- Pausieren und fortsetzen
- Im Pausenmodus eine Minute hinzufügen
- Im Pausenmodus den Timer löschen
- Laufenden oder pausierten Timer minimieren
- Gespeicherter Timerzustand und Wakeup-Alarm
- Helles, dunkles und Shake-Theme
- Unabhängig einstellbare Farbe des Ablesestrichs
- Einstellbares Vibrationsmuster und Alarm-Lautstärke
- Sekündliche Schwarz-Weiss-Umkehr der Alarmanzeige
- 8-Bit-Alarmschleife mit 10 Sekunden Ton und 2 Sekunden Pause
- Vollständige Bedienung über Hardwaretasten
- Open Source unter GPL-3.0

### Bedienung

#### Dauer auswählen

- Das vertikale Lineal verschieben, um ganze Minuten auszuwählen.
- Up oder Down für einzelne Minutenschritte drücken.
- Up oder Down für schnelle, fortlaufende Änderung gedrückt halten.
- Das Lineal loslassen, um über die V-Animation automatisch zu starten.
- Mit der mittleren Taste kann die gewählte Dauer ebenfalls gestartet werden.

#### Während der Timer läuft

- **Zurück / Steuerung oben links:** minimieren
- **Mittlere Taste / Steuerung Mitte rechts:** pausieren oder fortsetzen
- **Up / Steuerung oben rechts im Pausenmodus:** eine Minute hinzufügen
- **Down / Steuerung unten rechts im Pausenmodus:** Timer löschen
- **Mittlere Taste während des Alarms:** Alarm beenden und zum Lineal zurückkehren

### Kompatibilität

Swiss Chronograph wird ausschliesslich für die **Pebble Time 2** gebaut,
getestet und unterstützt:

```json
"targetPlatforms": ["emery"]
```

Andere Pebble-Plattformen werden nicht unterstützt.

### Bauen

Voraussetzungen:

- Pebble SDK 4.9 oder neuer
- Node.js und npm

```bash
npm install
pebble clean
pebble build
```

Installation über die Pebble-Entwicklerverbindung:

```bash
pebble install --phone <watch-ip>
```

### Ursprung und Lizenz

Swiss Chronograph basiert auf **Instant Timer** von Andrew Howe und
wurde von Maru Kitano umfassend neu gestaltet.

Das Projekt steht unter der
[GNU General Public License Version 3](LICENSE).

Die Urheberhinweise zum ursprünglichen Projekt und zu den Änderungen sind in
[NOTICE.md](NOTICE.md) dokumentiert. Der mitgelieferte Alarmton und seine
separate Lizenz stehen in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
