# AGENTS.md

## Progetto
Controller CNC touchscreen eseguito su Raspberry Pi, con Teensy 4.1 dedicato alla comunicazione e al controllo a basso livello della macchina.

Obiettivi principali:
- mantenere la GUI stabile e reattiva su Raspberry Pi
- evitare regressioni nella comunicazione seriale e nel comportamento della macchina
- eseguire modifiche piccole, leggibili e facili da verificare

## Hardware e ambiente
- Raspberry Pi 5
- Teensy 4.1
- Python
- GUI Tkinter
- Porta seriale: `/dev/serial0`
- Baud rate: `115200`

## Struttura attuale
- `dro_gui_tkinter_v16.py`: file principale dell'applicazione
- `manuale_v8.py`: schermata di controllo manuale
- `spianatura_xy_v5.py`: schermata spianatura XY
- `keypad_numeric_overlay.py`: tastierino numerico touch
- `old1/`: archivio versioni precedenti; non usarlo come base principale salvo richiesta esplicita

## Regole di sicurezza
Prima di modificare uno qualsiasi dei seguenti aspetti, fermarsi e chiedere conferma esplicita:
- protocollo UART o seriale
- formato dei comandi di movimento
- homing
- gestione dei limiti/finecorsa
- logica feedrate o velocità
- conversione assi o scala mm/count
- comportamento enable/disable driver
- comportamento di arresto di emergenza
- assegnazione pin
- comportamenti sensibili ai tempi
- firmware Teensy

## Regole di modifica
- preferire patch minime
- modificare solo i file necessari alla richiesta
- non rifattorizzare codice non collegato alla modifica richiesta
- non rinominare file salvo richiesta esplicita
- non cambiare comportamento esistente se non richiesto
- indicare sempre quali file sono stati modificati e perché
- se una modifica può influire sul movimento macchina, chiedere conferma prima di procedere

## Regole per l'interfaccia
- mantenere il flusso touchscreen semplice e prevedibile
- preservare la compatibilità con il display e l'ambiente Raspberry Pi
- evitare dipendenze pesanti salvo approvazione esplicita
- mantenere le modifiche grafiche localizzate quando possibile

## Igiene del repository
- non aggiungere file generati automaticamente salvo richiesta
- evitare di committare file temporanei o cache
- evitare nuovi file `__pycache__` nei commit futuri
- usare un branch per ogni modifica non banale

## Aspettative sul lavoro
Quando viene richiesta una modifica:
1. riassumere brevemente cosa si intende fare
2. applicare la patch più piccola possibile
3. indicare i file toccati
4. suggerire come testare la modifica sul Raspberry Pi
5. segnalare rischi, dubbi o assunzioni

## Stile di lavoro richiesto
- essere conservativi
- non fare cambiamenti architetturali ampi senza approvazione
- se c'è incertezza, chiedere prima di modificare logiche legate al movimento
- privilegiare sicurezza macchina e semplicità di rollback rispetto all'eleganza del codice

## Interpretazione tipica delle richieste
Se l'utente dice:
- `analizza il repository` -> non modificare file
- `modifica solo il file X` -> toccare solo il file indicato, se possibile
- `non toccare Teensy/UART` -> evitare completamente modifiche a protocollo, seriale e firmware
- `patch minima` -> fare la modifica più piccola possibile senza pulizie non richieste