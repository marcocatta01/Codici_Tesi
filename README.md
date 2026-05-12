# Codici Tesi

Questa repository contiene i codici della tesi "Motion Reconstruction of Planetary Exploration Rovers using Neuromorphic Cameras".  
La pipeline del problema è illustrata nella seguente figura:
<p align="center">
  <img width="500" alt="Screenshot 2026-03-19 113327" src="https://github.com/user-attachments/assets/dc84aa12-c305-4f6d-8df8-234dbf6cb077" title="Architecture"/>
</p>

Facendo riferimento all'immagine, i diversi software sono così localizzati:

|Elemento|Software|Posizione|
|:--:|:--:|:--:|
|Trajectory|Python|Cartella Unreal Engine|
|FMU|OpenModelica|Cartella Unreal Engine|
|Grounding, Environment, Simulation|Unreal Engine|Cartella Unreal Engine|
|Event Generation|v2e|Non inserito: usata la repo GitHub relativa|
|Filtering, Sensor Fusion, Optimization|Python|Cartella Python|

## Unreal Engine

La cartella di Unreal Engine contiene tutti i file relativi alla simulazione.  
La struttura principale è quella del simulatore [PROXSIMA](https://re.public.polimi.it/handle/11311/1278511); pertanto, alcuni dei file del simulatore originale, che non hanno subito modifiche, non sono stati inseriti. Si invita quindi ad usare questa repository come aggiunta al simulatore PROXSIMA, andando ad aggiungere o sostituire i file necessari.  
I file contenuti nella cartella sono:
1. Examples: contiene i file in input
  * Assets: contiene le FMU (Rover.fmu e Rover.mo sono quelli relativi alla tesi);
  * ondemand_mode e sequence_mode: file identici a quelli di PROXSIMA;
  * closed_loop: contiene i file JSON di configurazione (Simulation_Marco.json è quello relativo alla tesi) e una cartella Trajectory Generation che contiene diversi file per la generazione della traiettoria in input.
2. Source: contiene tutti i file sorgente della simulazione. In questo caso, tutti i file sono stati aggiunti, siccome ci sono state delle modifiche in diversi file. Quindi si consiglia di sostituire interamente la cartella "Source" di PROXSIMA, comunque utilizzabile per confrontare le differenze tra i file (partendo da PROXSIMAGameInstance e dalla cartella Levels).
3. PROXSIMA.uproject

NB: per questioni di dimensioni dei file, non è inserito il pacchetto RealisticDesertPack (reference), necessario per il caricamento dell'environment. Il pacchetto è scaricaabile direttamente dallo store di UE e, una volta fatto, va inserito nella cartella Content (qui non presente perché l'unico cambiamento è proprio la cartella del pacchetto).

# Python Pipeline

La cartella di Python contiene tutti i file relativi alla ricostruzione del moto e della mappa locale.
