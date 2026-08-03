# Tap Timer

[English](README.md)

Tap Timer ist ein haptischer Countdown-Timer für Pebble-Smartwatches. Auf
Touch-Modellen wird die Dauer direkt an einem vertikalen Minutenlineal gewählt,
statt sie über Menüs einzugeben. Jede Minute ist ein stabiler Rastpunkt: Erst
beim Überschreiten der Mitte rollt das Lineal sauber zur benachbarten Minute.

Nach dem Loslassen setzt sich die Auswahl, die Pfeile füllen sich und der
Countdown startet automatisch.

## Funktionen

- Vertikales Touch-Lineal von 0 bis 180 Minuten
- Präzise Ein-Minuten-Rastung mit weicher mechanischer Rollbewegung
- Fester Ablesestrich mit animierter Blase um Fünf-Minuten-Zahlen
- Automatischer Startablauf mit animierten Pfeilen
- Eigene kontrastreiche PPF-Pixelschrift
- Ruhige `MM:SS`-Anzeige als Standard
- Optional Hundertstelsekunden unter 100 Minuten
- Hell-, Dunkel- und Schüttelmodus
- Unabhängig einstellbare Farbe des Ablesestrichs
- Einstellbare Alarmvibration und optionaler Signalton
- Dauerhaft gespeicherter laufender oder pausierter Timer

## Bedienung

### Dauer auswählen

Auf einer Touch-Pebble wird das Lineal mit dem Finger verschoben. Es bleibt
immer exakt auf einem Minutenstrich stehen. Nach dem Loslassen beginnt die
automatische Startanimation.

Die Hardwaretasten bleiben nutzbar. Klassische Pebble-Modelle verwenden die
tastenbasierte Bedienung.

### Während der Timer läuft

- **Oben links:** Timer minimieren
- **Mitte rechts:** pausieren oder fortsetzen
- **Oben rechts im Pausenmodus:** eine Minute hinzufügen
- **Unten rechts im Pausenmodus:** Timer löschen

## Einstellungen

Die Konfigurationsseite auf dem Smartphone enthält nur noch Einstellungen, die
für die aktuelle Oberfläche benötigt werden:

- Vibrationsmuster des Alarms
- Lautstärke des Signaltons auf Modellen mit Lautsprecher
- Hell-, Dunkel- oder Schüttelmodus
- Optionale Hundertstelsekunden
- Farbe des Ablesestrichs

Die Hundertstelsekunden sind standardmässig ausgeschaltet. Auch bei aktivierter
Option werden sie ab 100 Minuten ausgeblendet, damit die eigene Pixelschrift
gut lesbar bleibt.

## Kompatibilität

Das Paket unterstützt die vorgesehenen Pebble-SDK-3-Plattformen. Das vertikale
Lineal benötigt ein Touch-Modell; Geräte ohne Touch behalten die
Tastenbedienung.

## Bauen

Voraussetzungen:

- Pebble SDK und Kommandozeilenwerkzeuge
- Node.js und npm

Clay-Abhängigkeit installieren und App bauen:

```bash
npm install
pebble clean
pebble build
```

Installation auf einer Uhr mit aktiver Entwicklerverbindung:

```bash
pebble install --phone <watch-ip>
```

## Ursprung und Lizenz

Tap Timer basiert auf **Instant Timer** von Andrew Howe und wurde von Maru
Kitano umfassend neu gestaltet.

Das Projekt ist freie Software unter der
[GNU General Public License Version 3](LICENSE).
