# Mòdul Python per a depurar

Aquest mòdul fa una implementació bàsica del simulador utilitzant
Python, SDL1.2 i OpenGL amb l'objectiu de poder depurar el simulador:

- Pantalla amb resolució original
- Controls "hardcodejats":
  - UP: W
  - DOWN: S
  - LEFT: A
  - RIGHT: D
  - A: O
  - B: P
  - START: Espai
  - SELECT: Retorn
- No es desa l'estat
- **CTRL-Q** per a eixir.

Per a instal·lar el mòdul

```
pip install .
```

Un exemple bàsic d'ús es pot trobar en **exemple.py**:
```
python3 exemple.py ROM.gbc
```

En la carpeta **debug** hi ha un script utilitzat per a depurar.
