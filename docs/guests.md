# The guest characters across the three discs

Survey of 2026-10-29 (Soulcalibur II USA: Xbox, GameCube `GRSEAF`, PS2
`SLUS_206.43`). [V] verified from the bytes, [I] inferred. The rough survey
scripts and listings are in `private/guest-survey/` (not committed).

## One container on all three

`root.olk` is the same "olnk" container on every disc, little-endian even on
the GameCube, with the same index tree:

```
+0x00 u32 count   +0x04 'olnk'   +0x08 u32 0x800 (alignment)   +0x0C 0
+0x10 u32 header size (data base = archive start + this)
+0x14 u32 total size (or 0)   +0x18 u32 unix time   +0x1C 0
+0x20 count x { u32 offset, u32 size, u32 time, u32 0 }
```

No names or hashes (entries are addressed by index), size 0 marks a hole,
0x800 alignment, one nesting level, no compression. [V] Inside, "numbered
packs" are `u32 count, u32 offsets[count+1]` in the platform's byte order
(big-endian on the GameCube).

| Group | Contents |
| --- | --- |
| `/1` | Command lists (move names); same on all three, no guest entry |
| `/2` | UI, select screen, profiles, art, select voices, per character ID |
| `/3` | Stages; pack type and indices differ per platform |
| `/4` | Characters (plus common motion at `/4/0..2`, type-09 packs at `/4/8..41`) |

## Where a character lives

Character k is a block of 39 slots at `/4/(46 + 39k)`: costumes (`0e` packs:
model and textures, params, sound, `lpb`, `vtb`, default weapon), 12 weapons,
the moveset pack (move data, motion bank, cameras), a smaller second moveset
pack and seven voice/effect sound banks. [V]

**The guest is always block 23, `/4/943..979`**: Spawn on Xbox, Link on the
GameCube, Heihachi on the PS2. [V, from decoded textures] In `/2`, character
IDs 19 (Link), 20 (Heihachi) and 21 (Spawn) are separate, and each disc fills
only its own guest's. [V pattern; names I] Each executable names only its own
guest. [V]

## What converts and what does not

- **Moves and motion are shared data.** For every regular character the
  moveset and motion packs are byte-identical between Xbox and PS2, and the
  GameCube move data is byte-identical too. GameCube motion differs only by
  16- and 32-bit byte swaps (99% of words). [V] So Heihachi's moveset should
  drop in unchanged [I], and Link's needs a byte-swap pass that 21 shared
  characters can test byte for byte.
- **Models:** Xbox `VMX` (vertex and index buffers), GameCube `VMG` (the same
  floats big-endian, drawn with GX display lists), PS2 an untagged VIF/VU
  packet format. Link converts mechanically; Heihachi's models need the PS2
  format reverse engineered (22 shared characters to check against).
- **Textures:** Xbox `VXT` (DXT1/3/5, P8), GameCube `VGT` (CMPR plus separate
  I4 alpha, which converts to DXT losslessly), PS2 GS uploads.
  **The Xbox textures are not higher resolution than the GameCube's:** the
  same sizes per character and no more large textures overall; the Xbox UI
  screens are smaller (512x512 against 640x448). [V] The Xbox version's
  advantage is 720p output, not assets.
- **Sound:** `mm?` banks (`XHDp`/`CHDp`/`PHDp`). GameCube is DSP-ADPCM at
  24 kHz [V]; Xbox and PS2 codecs not yet decoded. Each guest needs a re-encode.
- **Type-09 packs** (`/4/8..41`, purpose unknown): the PS2 disc carries
  variants for Link and Spawn as well as Heihachi, a hint that the engine was
  built to address all three guests. [V data; meaning I]

## Code work in the Xbox build

Room for two more character blocks (or block 23 remapped per selected guest),
the `/2` assets for IDs 19 and 20, longer name and profile tables, the select
grid, Weapon Master and ending hooks. In a static recompilation this is plain
C. **Biggest unknown:** whether Link's bombs, arrows and boomerang are move
data spawning generic objects or GameCube-only code. No guest-specific
identifiers were found in strings; the place to look is comparisons against
IDs 19-21 or block 23 and the object/effect spawn handlers.

## The character block, slot by slot (2026-10-29)

From a character every disc has (block 0), Xbox against GameCube
(`tools/guests/olnk.py` reads the containers and packs):

| Slots | What | Xbox vs GameCube |
| --- | --- | --- |
| 0-2, 8-10 | costumes: 14-part packs | see below |
| 16-27 | the 12 weapons: model, `mm?` textures, 1 KB of data | model and textures converted |
| 28 | moveset pack: move data, motion bank, a third part | move data identical; motion converted; third part a plain 32-bit swap (99.7%) |
| 29 | second moveset pack, same shape | as 28 |
| 30-36 | sound banks (`mmx` / `mmg`) | re-encode |

A costume pack: 0 the model (`VMX` / `VMG`), 1 640 bytes of mixed 16- and
32-bit fields, 2 an `mm?` container, 3 7 KB of mostly 16-bit fields, 4 `lpb`,
5 `vtb`, 6 (2 KB) identical, 12 an `mm?` container (textures), 13 the
default weapon.

**Motion banks** (moveset part 1): `u32 count, u32 id, u32 offsets[count]`,
identical apart from byte order. A motion: `u16 frames, u16 size?, u32 flags
(0x11080, 0x11084, ...), u64 track mask` (one bit a bone), then blocks of
16-bit values (byte-swapped on the GameCube) each followed by a bit-packed
stream (identical). The blocks' lengths follow per-track fields; a converter
has to know the layout, which the Xbox decoder states. Checked against the
21 characters both discs share (9223 motion pairs, the same sizes), a
converter is right when every GameCube motion becomes the Xbox's bytes.

Plan, Link first (his data is the closest to the Xbox's): the motion
converter (from the Xbox decoder's layout), models (`VMG` to `VMX`: the same
floats, GX display lists to index buffers), textures (`VGT` CMPR to DXT1),
sound, then a `root.olk` built on the player's machine from their discs with
Link in the guest block, read by the game in place of the disc's. Heihachi's
moveset and motion are the Xbox's bytes already; his models and textures
need the PS2 formats worked out.

## Models and textures, GameCube to Xbox (2026-10-29)

`tools/guests/vmg2vmx.py` rewrites a `VMG` as a `VMX`; `check_models.py`
compares the result with the Xbox's own models of the shared characters
(headers, nodes, joints, names, stamps exact; materials differ only in a
0x08000000 flag the Xbox sets on a few). `textures.py` converts the bank:
CMPR colour blocks kept (DXT1, the colour half of DXT3/DXT5), I4 alpha as
DXT3 (exact), CMPR alpha encoded as DXT5 alpha, RGBA8 encoded as DXT5. C8
(paletted) is not done yet; Link does not use it.

Found the hard way, all now in the writer:

- **Texture offsets.** The Xbox keeps every texture at its GameCube data
  offset less the difference in table size. The bank must keep that layout.
- **Stage records** (material +0x20) are per-material lists of `u16 kind,
  u16 size, u32s`, each list ended by `0xffffffff`; the game walks them
  (0x23820) and loops forever on a bad size.
- **Skinning** (0x23c30): vertices with 2 influences are skinned four at a
  time, 3 influences two at a time (a last group runs past its class). The
  last class holds every vertex with 4 or more influences, sorted by count.
  Each vertex's last bone word is `bone | (more << 8)`, the number of extra
  influences the next vertex has. The class ends with an entry of NaNs, which
  the game reads even when the class is empty.
- **Winding.** GX strips wind the other way from Direct3D: every triangle is
  turned over. Unturned, the model draws its inside and looks unlit.
- **Vertex order.** As the Xbox's: by class, then by GameCube position.

In game (with `--disc-file root.olk=FILE`): Talim's converted costume
model plays like the Xbox's; **Link in Talim's block** (his converted model,
textures, poses, moveset and weapons; sound banks still borrowed) loads into
a match and fights. Open: Link's own default weapon (costume part 13, which
has a skinned mesh) stops the run with a non-finite world matrix; Talim's
converted default weapon works. Sound banks (`mmg` to `mmx`, costume parts 2
and 12, weapon part 1) are the next format.

## Sound banks, GameCube to Xbox (2026-10-29)

`tools/guests/sound.py` turns an `mmg` container into an `mmx` one. It decodes
the DSP-ADPCM samples and encodes them as Xbox ADPCM (IMA, 36-byte blocks of
64 samples), and translates each sound's 0xb0 GameCube record into the Xbox's
0x58 parameter record and 0x114 format record (a WAVEFORMATEX, the data
offset and size, constants). Sounds that share samples share them, as on the
Xbox. Headerless sub-banks (`lpb`, with the voices) are the same bytes on
both. `check_sound.py`: of the shared characters' 247 banks, 230 come out
with byte-identical headers and records. The rest hold placeholder sounds the
Xbox stores at 1000 Hz, plus one parameter (10/60 at +0x0a/+0x50) whose rule
is a guess that is right 95% of the time. Converted audio against the Xbox's
own encoding: 19-22 dB SNR (two separate 4-bit encodings of one source).

Link in a match, with his own model, weapons, moves and sounds: runs; his
voice clips correlate with the captured match audio (0.5); a person has not
listened yet.

The NaN matrices behind Link's default weapon: three weapon joints (ids
26-28) are animated in only 23 of his 370 motions and otherwise keep a zero
scale; the game's inverse-scale step (0x69370) then writes NaNs, which the
hardware draws as nothing. The draw adapter now drops such draws instead of
stopping.

## The third mesh group is a triangle list (2026-10-30)

Mesh kind 0x10000 (the third group: hair, capes, whatever the game sorts by
triangle) indexes a **triangle list**, not strips. Written as joined strips,
the game drew garbage triangles: Link's two such meshes were wrong until now
(`vmg2vmx` and `guest/vmx.cpp` write lists; their triangles keep the strip's
own facing, which agrees with every matching triangle of the 15 characters
both discs have).

## Heihachi: the PS2 disc (2026-10-30)

The PS2 `root.olk` is laid out as the others; Heihachi's block is block 23
(the guest's), his `/2` entries sit where the Xbox keeps Spawn's (and the
Xbox's text tables hold his texts already, between Link's and Spawn's).
Byte for byte the same on PS2 and Xbox for the 22 shared characters: costume
parts 1 and 3-11, weapon records, both moveset packs. What converts:

- **Models** (`vmp.py`, `vmp2vmx.py`). A PS2 model is little-endian: joint
  records (the Xbox's 0x40-byte records, as they are), mesh records (a joint
  palette, then per pass a command block: material, texture registers,
  flags, VIF packets), the texture bank, the stamp and names. Skinned
  packets interleave each vertex's influences (bone-space position and
  weight, the weighted normal, the bone's VU address), UV and ADC (strip
  restart), colour; rigid ones carry streams (position with ADC, normal,
  colour, UV). A skinned vertex weighing nothing is a **spacer** that only
  shifts the PS2's strip parity: kept in, its triangles reach the world's
  origin (Cassandra's long dark bars). The PS2's passes are the Xbox's mesh
  groups. Converted against the Xbox's own models: the same influences
  float for float, 97% of triangles facing the same way (the PS2 draws both
  sides; each strip is turned to follow its normals in the bind pose).
- **Node matrices and the skinned streams' contents are not read by the
  game**: Cassandra drew the same with identity nodes and zeroed streams.
  The bind pose (needed to orient strips) comes from the joints: quaternion
  after rest angles in turns (+0x20, about z, y, x), translation +0x10,
  parent byte +0x3d; the two roots' places come from the motions.
- **Materials**: lit (`7`) or not (`3`, `1`), an environment pass after
  (`9`) is the Xbox's second texture with a sphere-map stage. Specular power
  is a table from the PS2's curve (`_POWER`).
- **Textures** (`ps2tex.py`, `gs.py`): banks of GS upload lists and register
  lists; the images are uploaded as 32-bit and read back as 8-, 4- or 32-bit
  through a model of the GS's memory (page, block and column layouts). A
  register list's GIF tag names its upload list (byte +9) and the image's
  real size (+0x0c). Large images come in several DMA chunks: one transfer.
- **Sound** (`sound.py`): `PHDp` banks of 0x50 records, PS-ADPCM. The fields
  map onto the Xbox's records (learned from 27440 shared sounds); the pitch
  at +0x18 is 4096 for 48 kHz; 32-byte sounds are placeholders. Converted
  audio and the Xbox's own correlate in spectrum (0.8) but not sample by
  sample: the Xbox's are separate encodes of the same recordings.
- **Strings**: from SLUS_206.43 (Heihachi, 平八, HEIHACHI, Iron Fist,
  Japan?, 75), written over Spawn's at start (`--guest-heihachi`).

In game: Heihachi is picked in the guest's place, fights in Practice, and
the VS screen shows his portrait. Still Spawn's: the gold names drawn by the
select and VS screens (as with Link).

## Weapon-bone modes: the program's per-character switch (2026-10-30)

The fighter setup sets nine weapon-bone modes, one for each weapon bone 21-29, at
the fighter's +0xb54. It does this in a switch on the character ID: the player's
+0x5a, or +0x5c when +0x58 is 0x19, minus 3. The switch is at 0x478c8 on the
Xbox, 0x800a0e00 on the GameCube and 0x196ed0 on the PS2. The motion code copies
the modes each frame (0x7f3cb). IDs with no case take the default, which is
9, 0xe, then zeros.
The values seen are 3, 8, 9, 0xc, 0xd, 0xe and 0x17-0x1e. 9 and 0xd are the
two hands: a right-handed fighter has 9, 0xd. A zero leaves the bone where the
motion puts it, at the fighter's root when the motion has no track for it.

The guest slot's ID is 0x1d, and it takes the default. Link's GameCube case
(ID 27) is left-handed: 0xd, 9, 3, 9, 9, 9, 0xd, 0xd. With Spawn's default, his
shield (bone 22) stood at a fixed point in the stage, and his quiver and arrow
(bones 27, 28) followed his feet. runtime/sc2_guest.cpp now writes Link's values
from a hook on 0x44360, which the setup calls right after the switch.

The GameCube case also calls 0x800a1cc4 (by weapon, though it reads no
argument): it fills a holder at the fighter's +0x3af0 that nothing reads (see
below). The PS2 has no case for Heihachi, so the default is his own.

## Characters beside Spawn (2026-10-30)

All three versions number their characters the same way: Link is 27, Heihachi 28
and Spawn 29. Each disc keeps empty places for the other two guests, both in /2
(each character's own entries) and in the program's per-character tables. The
switch at 0x478c8, for example, still has a case for 28.

tools/guests/roster.py builds the file, and guest/build.cpp
(`build_guests_root`) is its byte-identical port, used by the launcher:

- **/4.** Two blocks are appended after Spawn's: Link in block 24 (entries
  982-1020, all four costumes) and Heihachi in block 25.
- **Profile entry.** Slot 38 of each block holds the guest's profile, taken from
  its own program: 'GPRF', the profile record (0xf8 bytes, Xbox byte order, with
  string offsets into the entry), the gold select-name glyph string, and the
  select-grid cell.
- **/2.** Each guest's entries go in its own places (Link's at 84, 146, 178,
  284, 422, 470-472; Heihachi's at 85, 147, 179, 285, 423, 473-474).
- **Icons.** The grid icons are appended to /2/5 part 2 (from 75), and the large
  icons to /2/9 and /2/10 part 0 (from 146 and 148).
- **Silhouettes.** The GameCube's version-3 VGT silhouettes are now read.

`--guests FILE` (runtime/sc2_guest.cpp, `ApplyGuestRoster`) fills rows 27 and 28
of these tables:

| Address | Table |
| --- | --- |
| 0x19F430 | /4 block start (s16) |
| 0x19F474 | second-costume flag |
| 0x19D438 | profile |
| 0x1A2450 | glyph name |
| 0x1A07F0 | allowed in each mode |
| 0x17A7C8 | grid cell |

The grid (0x17A390) is 5 columns of 6 cells of 36 bytes; the Xbox fills four
columns, Random in cell 26. The guests first go in the empty fifth column (Link
in cell 4, Heihachi in cell 9), then the grid is laid out again in rows of five
(sc2_guest.cpp, `BalanceGrid`): every character in reading order, the guests
after Spawn, 25 characters in five full rows. Random keeps cell 26, which the
program names in several places (0x688E0, 0x6B000, 0x6B380, 0x6B530); it is
drawn under the middle column (`cell_place`). Each character's cell (0x17A7C8)
follows; IDs the grid does not show point at cell 29, empty in every layout.
Their rows in the 24-character roster index (0x19CA20) are left alone, so the
save layout does not change.

The cursor's move is 0x6B000(player, pressed), the cursor's vtable entry +0x20
(vtable 0x17AF70; the cell at +8, the cells it may take +4 to +6, live while
+0x14): Up and Down go five cells, Left and Right one, each wrapping in its
column or row and passing over empty cells and locked ones (the cell's check
routine at +4 says no). `cursor_move` (sc2_hooks.c) sends Down from a column's
lowest open cell, and Up from its highest, to Random, and from Random Up and
Down into the middle column; it sets the cell through 0x6B380(player, cell),
which moves the cells' highlights, and the original runs on without the
vertical press (its return, the new cell, is how the caller sounds the move).

As its own character, Link gets his own profile and the GameCube's weapon-bone
case (0xd, 9, 3, 9, 9, 9, 0xd, 0xd). The shield is then on his arm and the
scabbard on his back.

Still to do:
- The Museum (seen with every item owned, SC2_TEST_ITEMS=0-277): Character
  Profiles, Exhibition Theater and Battle Theater take the guests as any other
  character. The Art Gallery (table 0x34E578: 45 slots a gallery, each a
  thumbnail in the gallery's bank and the /2 entry of its picture; grid set by
  0xDCFA0) now has the guests' pictures: the guests' file appends their
  thumbnails (roster.py; /2/395 Link's portrait, then Heihachi's portrait and
  his two illustrations, /2/396 Link's three illustrations), and the game fills
  the slots (sc2_guest.cpp, ApplyGallery): Hi-Res gets a fifth row (Link's and
  Heihachi's portraits, Heihachi's illustrations, as Illustrations has only three
  free slots), Illustrations Link's three. The pictures are /2/422-423 and
  470-474. append_p8 now stores a picture at the next power of two (56 x 56
  thumbnails in 64 x 64), as the disc does.
- Records and unlocks go by the 24-place roster (0x19CA20; 0x36A70 gives a
  character's place, 0x36A40 the character at a place). The guests now have
  places of their own past it, Link 24 and Heihachi 25 (sc2_hooks.c,
  `roster_place`, `roster_character`): the records that keep a character as its
  place in a byte (0xC53A0, 0xC5A20, 0xC5FF0: Arcade, Survival and the others;
  0xFF for none) keep the guests', and the Records screens (0xC6600-0xC7F20)
  show them by name and icon, through 0x36A40 and the character's own tables.
  Where a place indexes a 24-entry array the guests keep the guest slot's: their
  plays count as Spawn's (0xC4940, the usage ranking's), their own item and
  weapons are Spawn's (0x569F0, 0x566F0), and a Weapon Master table (0xF1390) has
  no entry for them, as before. Their own play counts would need room the save
  does not have.
- The result screens' caption (0xD24F0: Survival, Time Attack and the others)
  takes the character's name from a table of 24 rows by ID (0x34A9E0; the
  program keeps six copies); for a guest Spawn's row holds the guest's name while
  it runs (`result_name`), where it showed what the buffer last held. The list of
  characters beaten in a run (0xD29A0, counts by ID at 0x398B8C, rows from the
  table's second copy) lends a beaten guest the row of a character beaten 0
  times (`beaten_list`; not yet seen on screen).
- Practice's Command List and Show Move work for both (their move names and
  inputs from their own discs), and both come up as the computer's opponents in
  Survival, in either outfit, Link's bombs and all.
- Weapon Master's select (scene 26; 0xFA030 builds, 0xFA430 runs a frame) is
  a 5 x 4 grid of the table at 0x36C490 (character ID, outfit), its sprites in a
  pool of 64 slots sized for 20 cells. The guests stand in a sixth column beside
  the first two rows (runtime/sc2_wm_select.c): their sprites made after the
  build as its loop makes them, Right from the last column into it, and while
  the cursor is there the moves, outfit and choice done as 0xFA430 does them
  (its pad reads masked). Chosen, a guest plays Weapon Master's missions.
- Link's item weapons (0x68-0x6A): not a gap. The GameCube's case calls
  0x800A1CC4 for every weapon with an argument the routine never reads; it looks
  up a data entry for the character into a small holder at the fighter's +0x3AF0
  (data, a float, a vtable whose two methods are destructors), and nothing in
  the GameCube's program reads +0x3AF0 after (searched as an offset, an address
  and through a sub-structure). Link's weapons on the Xbox lack nothing of it.

Arcade with a guest (tested with Link, to the end): the final boss takes a form
by its opponent's character (0x528E0, a table of IDs 1-29); 27 and 28 take the
guest slot's (Spawn's), where form 0 hung before the final stage. The ending's
row by character (0x34ED88: picture, lines + 1, first text, 0) is filled for
both: Link (84, 10, 77), Heihachi (85, 12, 81, as the PS2's table has it); the
texts were already in /2/87.

Weapons (the Extra modes' weapon select): weapon w (2-11) of a character is item
86 + 10 x its roster place + w (0x566F0). The guests have no roster place and
landed on items 78-87, which are other things; they take Spawn's (228-237), so
their weapons are had as Spawn's are. With those items owned, Link's eleven
(Master Sword to Bug-Catching Net, names and texts from his disc) are offered
and fight.

The fifth column's frame: the grid panel sprite is drawn in three pieces so it
reaches one column further (sc2_hooks.c, `sprite_draw`).

The game sizes its reads of root.olk from the directory listing of D:\. The
runtime now lists an overridden file at its own size; before that, a guests file
longer than the disc's root.olk read short and the game hung.

## Their profile unlocks (2026-11-01)

Clearing Arcade unlocks the character's profile: an item (kind 3) from the table at 0x1A2300 (Talim 22 -> 11),
announced with its picture at `/2/297 + item` ("Congratulations! The profile for Talim is now available."). The
Xbox table has none for 27 and 28, and the item records 48 and 49 (0x1A1FF8, 8 bytes each) are the only empty ones
below 0x58. They are the guests' (`sc2_guest.cpp` writes their records; the hook on 0x569B0 answers them for 27
and 28), and the guests' file puts each guest's profile picture at `/2/345` and `/2/346`: the GameCube's and the
PS2's `/2/343`, the guest slot's (`roster.py` LINK_UI and HEIHACHI_UI, `guest/build.cpp`). A guests' file built
before has no picture there, and the game would wait for it for ever, so the items are given only when the file
has them. The Museum's profile viewer asks for the same item: a guest's profile is unlocked by clearing Arcade
with them, as everyone's is (with the Xbox's -1 it was always open).


## The file's version (2026-11-02)

The builders' output has a number, `GUESTS_VERSION` in `guest/build.h`, written beside the file as
`guests-root.olk.version` (`sc2-guests N`). **Raise it in any change that alters what the builders write.** The
launcher's Guest page asks for a rebuild when a player's file has another number (or none: built before 2026-11-02),
and online play compares it with the file's size (`runner.cpp`, the match's build), so two players whose guests came
from different releases are told so rather than refused as "different builds". Version 1 is the output of
2026-11-01 (c141074). The Python builders (`tools/guests`) write no stamp.

## PS2 material alignment correction (2026-11-03)

Tag-7 material payloads start at the next 16-byte boundary after the four-byte
command header, relative to the model. They do not always start 16 bytes after
the command. Heihachi's translucent hair command at model offset 0x1d18 is
72 bytes long, with its 16 floats at 0x1d20. Both converters previously read
from 0x1d28, dropping two floats and padding the end with zeros.

That produced cyan ambient RGB (0,1,1), yellow diffuse RGB (1,1,0.5), alpha
0.431, negative specular components, and power 2. Correct decoding restores
ambient (0,0,0), diffuse (1,1,1,1), specular (0.5,0.431,0.38), and power about
20 from the original PS2 material. No texture recoloring or light suppression
is involved. Natural stage lighting and the source's warm highlights remain.

The C++ and Python parsers align the payload, read only complete RGBA quadwords,
and accept empty sections or trailing partial colours. Colour reads beyond the
input buffer are rejected.
Guest cache version is 2. Synthetic tests cover all four word alignments and
empty/partial payloads, the size-16 kind-9 section, and out-of-bounds
colour reads. Rebuilding the user's combined roster changed only 25 material
bytes in each of four Heihachi model variants (/4/1021,1022,1029,1030); every
other archive entry, model geometry, and texture remained byte-identical.

Regression commands: `python tools/test_ps2_material.py`; compile
`tools/test_ps2_material.cpp` against the guest libraries to test the production
C++ parser as well.

The Python converter uses explicit left-to-right addition for floating-point
sums, matching the C++ converter. Python 3.12 and newer changed the built-in
`sum` algorithm; using it could change near-zero bounding-sphere coordinates
and prevent byte-identical archives even though material decoding agreed.
The regression includes cancellation and signed-zero cases.

## One-sided sorted meshes face the Xbox's way (2026-11-06)

The locks framing Link's face (material 3, the third group's sorted
triangles) were missing in every costume, leaving dark angular gaps at his
temples. Material flag 0x08000000 is one-sided (back faces culled): the
Xbox's own models face nearly every such triangle along its normals (0.1%
otherwise), while two-sided materials mix both ways. The third group's
triangle list kept each strip's GameCube order, which is the Xbox's facing
for two-sided materials but the reverse for one-sided ones, so all 90 of
Link's material-3 triangles faced away and were culled. `_triangle_list`
(and `triangle_list` in `guest/vmx.cpp`) now turns one-sided triangles over.
`check_models.py` compared triangles as vertex sets, which a turned-over
triangle still matches; it now also counts each one-sided triangle's winding
against the Xbox's. For the shared characters, group 2: 0 of 394 facing the
Xbox's way before, 394 of 394 after; groups 0 and 1 unchanged (840 of 840 in
group 1; in group 0, 654 of 257,368 are reversed, as before). Guest cache
version 3 rebuilds older files.

## With the European Xbox disc (2026-11-14)

The European disc (Europe, Australia) takes the guests too, from the same USA GameCube and PS2 discs. What it
changes, from the bytes of both discs and both programs:

- **/2 (menus, pictures, text): 585 entries to the US disc's 516.** 0-87 are the same; 88-91 hold the Arcade
  endings' text in French, Italian, German and Spanish (each the English 87's 88 strings, the guests' at 77-84
  as there) where the US disc has 88-94 empty; every entry from the US disc's 95 on is one lower (the guests'
  places 84/85, 145/146, 177/178, 283/284, 344/345, 421/422, 469-473 are empty, as theirs are on the US disc). The
  last 69 are the other languages' weapon names, profile texts and pictures. `guest/build.cpp`'s `Layout`
  (and `tools/guests/roster.py`'s) maps the US numbers; the guest discs keep theirs.
- **Large select-grid icons (/2/10 part 0): 154 to 148.** Six more come before the characters', so every
  character's large icon is 6 higher (Spawn's 106 and 107); /2/9 is unchanged. The guests' are appended at 154
  (`runtime/sc2_guest.cpp`, `kFirstNewLarge`).
- **The program's tables are the same:** profiles, block starts, second-costume flags, what each mode allows,
  the select grid (but for those icons), each character's cell, the items and the Arcade endings (their text
  numbers index the chosen language's entry). The Art Gallery's picture numbers are each one lower, as /2's.
- **/4 (characters)** has the same blocks; the movesets (slots 28-29) are a later build (July 2003 to the US
  disc's June) and Link's and Heihachi's US movesets play in it.

Each release's file is its own (`guests-root.olk`, `guests-root-europe.olk` on the Guest page). The program
leaves out a file built from the other release's disc (`GuestFileMatches`: its /2 count) and plays without
guests.

## Their weapons on the Extra modes' weapon choice (2026-11-14)

The weapon choice (after a character in the Extra modes) shows the chosen weapon alone, turning. Its drawing
(0x47DB0, the shown weapon from 0x47FB0) places the weapon's parts with 0x84F40, by character: {count, pointer} at
0x17D388 (PAL 0x181E68), each part a joint (u32), an offset and a rotation (3 floats each). The Xbox's table has
none for 27 and 28, so the guests' weapons were drawn with their parts never placed and did not show (both discs,
both guests). Each guest's own program has its row: Link's two parts at 0x802518CC on the GameCube (its table's row
27), Heihachi's two at 0x490960 on the PS2 (row 28). `runtime/sc2_guest.cpp` (`kWeaponParts`) puts them in the
table when the guests load. Fights did not need them.
