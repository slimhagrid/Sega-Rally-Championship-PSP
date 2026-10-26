# Sega Rally Championship (Saturn, USA) - the 2-player / VS modes (survey; stretch goal after M10)

Tags: **[V]** read / checked, **[I]** inferred, **[U]** unread.  Closes the M2 gap "2P / VS not read" at survey level.  The single-player port (`src/drive.c` ...) handles game types 0 and 2 only; this lists what the other types add.

## 1. Game types [V from the dispatch tables]

`[0x060AA860]` = 0 arcade, 2 practice / time attack (single player; ported), and **4, 6, 8, 10** (two cars on one console).  Every per-type table in the race code has the same six entries (type / 2 as index):

| code | type 0, 2 | type 4 | type 6 | type 8 | type 10 |
|---|---|---|---|---|---|
| `check_lap` (0x0601465C, jump table at 0x06014668) | 0x06014674 | 0x06014750 | 0x06014810 | 0x06014750 (= type 4) | 0x06014960 |
| `game_display` (0x060123F8, table at 0x06012408) | 0x06012414 | 0x06012470 | 0x060124D0 | 0x06012470 (= type 4) | 0x060124F0 |
| main per-pass handler (`plctrl`) | `normal_plmove` | `vs_plmove1p` / `vs_plmove2p` are the VS handlers; which type uses which [U] | | | |

Types 4 and 8 share their lap / display code; 6 and 10 differ [V]; what the four types are (VS race, VS with handicap, battle, time-trial duel ...) is [U] - the menu `cs_battle_2p` sets them (`bt_chg_*`: course, lap count, **delay**, **on/off**, **lag**: very probably the VS handicap / catch-up options) [I].

## 2. What the race loop does differently [V names, I behaviour]

* **Two car slots**: slot 0 at 0x060B0000, slot 1 at 0x060B0600 (the HUD code picks the screen half by `r14 == 0x060B0000`; every slot has its own R block at +0x500).  `vs_ctrl_plset`, `vs_plmove1p/2p`, `goali_plmove2p`, `goal_car_move2p`, `normal_rmove2p`, `player::vs_g_ctrl`, `car::vs_rank_calc` (the ranking / handicap), `vs2p_plmt`.
* **Master / slave split**: the slave SH-2 runs the second car (`gctrl::car_move2p_ctrl`, `m_car_move2p_ctrl` / `s_car_move2p_ctrl`, `car_disp2p_ctrl`) and half of the polygon work (`zsort::game_zsort_2p`, `mkpoly::draw_course_2p`, `mkpoly_init_2p`) - handler pointers with bit 0 set are run by the slave (docs/collision_world_ai.md).  On the PSP this is just two calls to the same car code.
* **Two views**: split screen, each half with its own camera (`camera::vs_cainit`, `camera2p_ctrl`, `calc_dispang2p`, `get_poly_map2p`, `view_changer2p`) and part list; the 2P course models are separate, smaller data (`DESERT2P.MAD/.MDL/.TEX/.CG/.CL` and the other three stages, `OBJECT2P.*`).
* **Display**: `re_1x1time_2p`, `lap_rewrite2p`, `retime_etc_display2p`, `count_down_start2p`, `reverse_display2p`, `result_display2p`, `dispTachoMeter_2p`; character data `CGDATAVS.DAT` / `TCGDATAVS.DAT`.
* **Lap code**: `lap_time_calc2p` (0x06014DD8; the entry the 1P stop-watch shares: it loads the minute limit 60 instead of 10, [V] from the fall-through at 0x06014DD8), the 2P results (`result2p_main`, `timeact2p_init`).
* **Sound**: `game_se_set2p` (engine word of player 2 at 0x25A007E0), `P2SDDRVS.DAT` (the 2P driver build) and per-car banks `P2C_CB / P2C_DB / P2C_SB / P2D_* / P2S_*` (one set per car, meaning of the letters [U]), the 2P voice set `GET2 OOH2 SMOKIN2 SMOOTH2 TAKE2 WAY2 WHO2 FENDER2` at a different sound RAM address (0x25A38000 / 0x25A39000).

## 3. Menus and data [V]

`cs_battle_2p` (8,996 bytes of code), `cs_battle_2p_pset` (4,284), `bt_pict_tbl` (3,064): the 2P setup screens, pictures `BT_2P.DAT` (53 KB) and `BT_REC.DAT` (the VS record screen); `bt_set_bt_2p` (442 bytes) writes the chosen settings.

## 4. What a port would need (after M10)

1. Generalise `Drive` to two instances (the code is already slot-pure: everything reads `car[]` of its own slot; the globals are the shared part: `ret_state`, camera, `msg45`).
2. The VS rules: `vs_rank_calc`, `vs_g_ctrl` (handicap / "lag" / "delay"), the type 4 / 6 / 8 / 10 `check_lap` variants and HUDs - each checked against the oracle (`saturn_env.Race` needs a 2-car mode: slot 1 init + the slave-run car handler).
3. Split-screen rendering: two `scene_draw` calls with scissor halves (the course part list of each car; 2P course models are lower detail).
4. The 2P menus and results.
Also **opponents** (single-player CPU cars, M7): `othercar`, `otherset`, `otherctrl`, `othersub`, `otherdat` (about 12 KB of code, the AI guide lines are in `phys.dat` already); the VS code shares the car-car collision (`carcoli`) with them.

## 5. Unread

All VS rule code (`vs_rank_calc`, `vs_g_ctrl`, type 6 / 10 lap code), the 2P camera, what each of the types 4 / 6 / 8 / 10 is, the 2P sound banks, the battle menu logic.  Not needed before M10.
