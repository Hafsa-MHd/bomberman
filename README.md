# Bomberman

Bomberman en console pour deux joueurs, sous Windows. L'arène est lue dans `arena.txt` (`#` mur, `X` obstacle destructible).

Chaque joueur a une bombe et une capacité :

- joueur 1, Gojo : ZQSD pour se déplacer, W pour poser une bombe, A pour la capacité
- joueur 2, Toji : flèches, Espace pour la bombe, P pour la capacité
- Echap quitte
- H affiche l'aide

## Lancer

Le code utilise la console Windows (`windows.h`, `conio.h`).

```bash
gcc -Wall -Wextra -o bomberman.exe main.c functions.c
bomberman.exe
```

`arena.txt` doit être dans le même dossier que l'exécutable.
