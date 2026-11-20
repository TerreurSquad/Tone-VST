# Entscheidungen & Abweichungen von HANDOFF.md

Kurzprotokoll eigener Entscheidungen (siehe HANDOFF §13). Neueste unten.

| # | Datum | Thema | Entscheidung / Begründung |
|---|---|---|---|
| D1 | 19.11.2026 | Repo | GitHub-Repo `TerreurSquad/tone-vst` (statt `xtone/`). Projekt-/Plugin-Name bleibt vorerst „XTONE“. |
| D2 | 19.11.2026 | JUCE | Submodule auf Tag **8.0.15** (neuester 8.0.x beim Start). |
| D3 | 19.11.2026 | Actions | `actions/checkout@v7`, `actions/upload-artifact@v7`, `softprops/action-gh-release@v3` (aktuelle Majors). Artefakt enthält direkt `XTONE.vst3` + `XTONE.exe` (kein ZIP im ZIP); das Release bekommt `XTONE-win64.zip`. |
| D4 | 19.11.2026 | Event-Handling | `Engine::process()` verarbeitet MIDI-Events sample-genau selbst (Offset im Engine-Takt). Der Prozessor teilt nur in ≤ 256-Sample-Chunks; ein zusätzliches Teilen an Event-Positionen ist dadurch unnötig (gleiches Ergebnis, T13). |
| D5 | 19.11.2026 | Jitter | Jitter wirkt auf die **Periode** (Faktor gleichverteilt, Mittel 1), nicht auf das Inkrement. Nur so bleibt die mittlere Rate exakt (ein Faktor auf das Inkrement würde die mittlere Periode um bis zu ~10 % verlängern). T7 prüft das. |
| D6 | 19.11.2026 | Phase-Reset | Startet eine Note aus dem Idle-Zustand (ADSR fertig), gibt es immer einen sofortigen Hit mit Phase 0 – auch bei `phase_reset = aus`. `phase_reset` wirkt nur bei neuen (nicht-legato) Noten während das Plugin noch klingt. |
| D7 | 19.11.2026 | Voice-Stealing | Pro Lane gibt es einen Zusatz-Slot für die gerade „gestohlene“ Stimme (0,5 ms Fade bei Overlap, `declick` bei Cut). Nicht ausklingende Stimmen ≤ `voices`; physisch kurzzeitig ≤ `voices + 1`. Das globale Limit 64 bleibt hart: `uni × (limit + 1) ≤ 64`, notfalls wird das Lane-Limit um 1 reduziert (z. B. 8 Unison → 7 + 1). |
| D8 | 19.11.2026 | Ausgangsstufe | Nach dem DC-Blocker wird (bei Clip Soft/Hard) **noch einmal mit derselben Kurve** geclippt, weiterhin auf OS-Rate. Grund: Der 5-Hz-Hochpass schwingt nach einer geclippten Stufe bei sehr tiefen Raten bis fast ×2 über (T14 schlug mit 1,92 fehl). Kein Clip nach dem Downsampling (§6.7 bleibt eingehalten). |
| D9 | 19.11.2026 | Interpolation | Außerhalb des Samples wird mit 0 aufgefüllt (Hermite liest beim Hit-Start den Punkt −1 als Stille). T4 erreicht −84 dB THD+N (ohne Sub-Sample-Start −47 dB). |
| D10 | 19.11.2026 | Tune-Snap „Basis“ | Snap-Offset wird auf die Zielnote (vor Glide/Bend/Rampe/LFO) berechnet und als Halbton-Offset addiert; Glide gleitet also zwischen gesnappten Tönen. |
| D11 | 19.11.2026 | Note-ADSR | Attack linear, Decay/Release exponentiell (Zeit = Abfall auf −80 dB). Release 0 = sofort aus. |
| D12 | 19.11.2026 | Parameter | Alle 86 Parameter sind ab M0 angelegt (stabile IDs). Funktionen späterer Meilensteine (A/B, Rampe, LFO, Sequencer) sind bis dahin ohne Wirkung. |
| D13 | 19.11.2026 | Marker-Editing | Einfaches Marker-Editing (Doppelklick/Ziehen/Rechtsklick, [Gleichmäßig], [Marker löschen]) ist schon in M1 enthalten; Zoom/Scroll und Transienten folgen in M2. |
| D14 | 19.11.2026 | Sample-Länge | Dekodierte Samples werden auf 10 Minuten (bei 48 kHz) begrenzt, um Speicherprobleme zu vermeiden. |
| D15 | 19.11.2026 | CI/pluginval | `pluginval.exe` ist eine GUI-Anwendung: Ein direkter Aufruf aus PowerShell wartet nicht und setzt `$LASTEXITCODE` nicht (der Schritt aus HANDOFF §12.2 schlug deshalb immer fehl). Jetzt `Start-Process -Wait -PassThru` + Prüfung auf `SUCCESS`/`FAILED` im Log. |
| D16 | 19.11.2026 | macOS-Fork | `Tone-VST-mac` baut VST3 und Standalone über das CMake-Preset `macos-universal` für arm64 und x86_64 mit Deployment-Ziel macOS 11. |
| D17 | 19.11.2026 | macOS-Bundles | `com.terreursquad.xtone` ist die Bundle-ID. Die VST3- und Standalone-Bundles werden nach dem Erzeugen ihrer Ressourcen ad hoc signiert. |
| D18 | 20.11.2026 | Wellenform & Transienten | Zoom und Scrollen beziehen sich auf Quell-Frames. Bei starkem Zoom zeigt der Editor einzelne Samples. Die Hintergrundanalyse ersetzt Marker bei einem Treffer. Wenn sich Sample oder Marker während der Analyse ändern, wird das Ergebnis verworfen. |
| D19 | 20.11.2026 | Audio Unit | Der macOS-Build enthält ein AUv2-Instrument (`aumu`, `Xtn1`, `Xtdv`) für Logic Pro. macOS-CI validiert es mit Apples `auval`. |
| D20 | 20.11.2026 | Rate-Ratio-LFO und Unison | Rate Ratio folgt der tatsächlichen Phase der ersten Lane einschließlich Jitter, Detune und Rate-Modulation. Die Phase wird nach der gewählten Tick-Anzahl am Subsample-Tick verankert. Zufällige Unison-Startphasen staffeln die Folge-Ticks. Der erste Hit beginnt in allen Lanes am Anfang des jeweiligen Playback-Fensters. |
| D21 | 20.11.2026 | Sequencer und State | Das Raster sitzt unter einer breiten Wellenform im eigenen Tab. „Clear“ setzt alle 32 Steps auf Pausen. Ältere States ohne SEQ erhalten 32 neutrale, aktive Steps. Die Audio-Engine übernimmt nur vollständige Patterns und behält bei gleichzeitigen Änderungen den letzten konsistenten Snapshot. |
