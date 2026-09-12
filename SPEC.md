Project Overview



Build a desktop application called Snap Tap that replicates the Snap Tap feature found on the Razer Huntsman V3 Pro keyboard. Snap Tap is a key priority system: when two opposite directional keys are held simultaneously, the most recently pressed key takes priority. When that key is released, the previously held key reactivates automatically.



Core Functionality

What Snap Tap Does:

When a user holds D and then presses A, the program instantly releases D and activates A

When the user releases A while still holding D, the program automatically reactivates D

This applies to any configured key pair, not just A/D

The switching must be near-instant with zero perceptible delay at default settings



Key Pair System

Keys are managed in opposite pairs (e.g., A↔D, W↔S)

Each pair operates independently

Multiple pairs can be active simultaneously

Default enabled pair on launch: A/D only

