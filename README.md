RogueLike

Dungeon Generation class
@author Subhajit Guchhait 
@Date   2nd February  

Details and Information -
This C class creates a random 80×21 dungeon with:
Up to 6 rectangular rooms ('.')
Corridors ('#') connecting the rooms
Two staircases ('<' for up, '>' for down)
Rock/unused cells (' ')

Features -
Dungeon Initialization:

An 80×21 2D grid is filled with rock (' ') and assigned a default hardness of 1.
Room Generation:

Up to 6 rooms are placed randomly.
Each room is filled with floor cells ('.').
Rooms do not overlap, and each is placed at least 1 cell inside the boundary.
Corridor Connection:

Corridors ('#') connect each newly placed room to the previous one, ensuring the dungeon is fully traversable.
Stair Placement:

Two staircases are placed: one up ('<') and one down ('>').
They appear on valid floor or corridor cells (not on rock).
Printing the Dungeon:
Monster Generation:
Monsters are randomly placed in the dungeon on floor tiles ('.').
Each monster has a bit-flag behavior type (0-15):
Bit 0 (1) - Intelligent: Uses Dijkstra’s pathfinding algorithm.
Bit 1 (2) - Telepathic: Always knows the PC’s location.
Bit 2 (4) - Tunneling: Can tunnel through walls.
Bit 3 (8) - Erratic: Moves randomly 50% of the time.
Speed is randomized between 5 and 20.
Monsters attack the PC when they move into its position.
Player Character (PC) Movement:
The PC ('@') is placed inside the first generated room.
Moves randomly in any direction.

If the PC dies, the game ends.

Dijkstra’s Algorithm for Pathfinding:

Two separate Dijkstra maps are maintained:

Non-Tunneling Map: Used by monsters that cannot tunnel.

Tunneling Map: Used by monsters that can tunnel, breaking walls over time.
The final dungeon is printed to stdout, displaying floors ('.'), corridors ('#'), rock (' '), up-stairs ('<'), and down-stairs ('>').

• The code can **save** and **load** the generated dungeon from a hidden directory located at `~/.rlg327/`:
  - **Save** your current dungeon by calling the program with the `--save` switch.
  - **Load** an existing dungeon from disk with the `--load` switch.
• If both `--save` and `--load` are provided:
  - The dungeon will first **load** from the file, 
  - Display it,
  - Then **save** it back to disk.


How to Run -
make                 - Compiles DungeonGeneration.c into an executable
--save: Saves the current dungeon to ~/.rlg327/dungeon.
--load: Loads a previously saved dungeon from ~/.rlg327/dungeon.
--nummon X Spawns X monsters in the dungeon (default: 10)
These switches may be combined (e.g., --load --save).
