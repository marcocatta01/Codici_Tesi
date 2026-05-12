# Codici Tesi

Questa repository contiene i codici della tesi "Motion Reconstruction of Planetary Exploration Rovers using Neuromorphic Cameras".  
La pipeline del problema è illustrata nella seguente figura:
<p align="center">
  <img width="500" alt="Screenshot 2026-03-19 113327" src="https://github.com/user-attachments/assets/dc84aa12-c305-4f6d-8df8-234dbf6cb077" title="Architecture"/>
</p>

La repository è divisa in due cartelle principali:
* Unreal Engine: contiene tutti i file relativi alla simulazione (environment, codici C++, traiettoria in input, FMU di OpenModelica, ...);
* Python: contiene tutti i file relativi alla ricostruzione del moto e dell'ambiente.

Facendo riferimento all'immagine, i diversi software sono così localizzati:

|Elemento|Software|Posizione|
|:--:|:--:|:--:|
|Traiettoria|Python|Cartella Unreal Engine|

