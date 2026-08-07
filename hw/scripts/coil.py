import numpy as np
import matplotlib
import matplotlib.pyplot as plt

# ============================================================
# MAGLEV COIL DESIGN + PARETO OPTIMIZATION (weight/volume vs power)
# ============================================================
# Physics recap: F(i,x) = sum over each real turn k of
#   1.5 * mu0 * m_dip * r_k^2 * i * x_k / (r_k^2 + x_k^2)^2.5
# We sum turn-by-turn rather than lumping into one "effective loop",
# since the coil is comparable in size to the hover gap.
#
# CONVENTION (must match the closed-loop simulator!):
#   x0 is the hover gap measured from the TOP SURFACE of the coil,
#   not from the coil's axial mid-plane. The closed-loop sim defines
#   force_per_amp(x) with "x is height ABOVE THE TOP OF THE COIL" and
#   converts internally via x_center = x + coil_top. We do the exact
#   same conversion here so that a design picked by this sweep, when
#   handed to the sim with the same x0, sees the same physical gap and
#   therefore the same Fpa/i0/P0 this sweep computed for it.
#
# THE TRADEOFF: for a fixed geometry, adding copper (more turns, and/or
# thicker wire) always helps power in one of two ways:
#   - more turns -> lower i0 needed for the same force (i0 ~ 1/N)
#   - thicker wire -> lower resistance per unit length (R ~ 1/d_wire^2)
# Both reduce power (P = i0^2 * R), but both also add weight/volume.
# There is no free lunch -- this script maps out exactly how much power
# you save per gram of extra copper, and finds the non-dominated
# (Pareto-optimal) designs: the ones where no OTHER design is both
# lighter AND lower power.
# ============================================================

# ---------------- CONSTANTS ----------------
mu0     = 4*np.pi*1e-7     # T*m/A
rho_cu  = 1.72e-8          # Ohm*m, copper resistivity (~30C)
dens_cu = 8960.0           # kg/m^3

# ---------------- LIMITS (hard feasibility bounds) ----------------
# NOTE: kept identical to the closed-loop sim's I_max/P_max so that
# "feasible" means the same thing in both scripts. (Sim previously used
# I_max=25, P_max=100; sweep previously used I_max=20, P_max=300 -- that
# mismatch meant designs accepted here could still blow the sim's power
# budget. Now unified on the tighter, sim-side numbers.)
V_max = 20.0
I_max = 25.0
P_max = 100.0
W_max = 5000.0      # maximum copper weight (g)
L_wire_max = 20.0 # maximum total wire length (m)
# ---------------- HOVER TARGET ----------------
x0 = 0.05                  # m, measured from the coil's TOP SURFACE

# ---------------- MAGNET ----------------
M_magnet  = 0.030           # kg
M_payload = 0.000           # kg (extra mass carried by the magnet)

M_total = M_magnet + M_payload

Br       = 1.32             # T, N42 typical
mag_dia  = 0.025            # m
mag_h    = 0.005            # m
V_mag  = np.pi*(mag_dia/2)**2*mag_h
m_dip  = Br*V_mag/mu0

# ---------------- FIXED CONSTRAINT ----------------
ID = 0.060                  # m, inner diameter is fixed; OD & height are free

# ---------------- DESIGN SPACE TO SWEEP ----------------
awg_range        = range(20, 22)            # thicker (8) to thinner (26) wire
n_layers_range   = range(1, 10)            # radial layers
turns_pl_range   = range(1, 1000)            # axial turns per layer

# ============================================================
# Core physics for one candidate design
# ============================================================
def awg_to_diameter_m(awg, insulation_factor=1.07):
    d_bare = 0.127e-3 * 92**((36-awg)/39.0)
    return d_bare*insulation_factor, d_bare

def build_design(awg, n_layers, turns_per_layer):
    d_wire, d_bare = awg_to_diameter_m(awg)
    R_in = ID/2
    radii    = R_in + d_wire*(np.arange(n_layers)+0.5)
    z_offset = d_wire*(np.arange(turns_per_layer) - (turns_per_layer-1)/2.0)
    RR, ZZ = np.meshgrid(radii, z_offset, indexing='ij')
    radii_flat = RR.ravel()
    z_flat     = ZZ.ravel()
    N = radii_flat.size

    # Coil axial extent, needed to convert "x0 above the coil top" into
    # the mid-plane-referenced coordinate the per-turn sum uses.
    # (Matches the closed-loop sim's coil_top = coil_height/2 exactly.)
    coil_height = turns_per_layer*d_wire
    coil_top = coil_height/2.0

    x_center = x0 + coil_top
    x_k = x_center - z_flat
    denom = (radii_flat**2 + x_k**2)**2.5
    Fpa = float((1.5*mu0*m_dip*(radii_flat**2)*x_k/denom).sum())  # N per amp
    if Fpa <= 0:
        return None  # degenerate geometry, skip

    i0 = (M_total*9.81)/Fpa

    wire_area    = np.pi*(d_bare/2)**2
    total_length = float((2*np.pi*radii_flat).sum())
    R_dc = rho_cu*total_length/wire_area
    weight_kg = total_length*wire_area*dens_cu

    V0 = i0 * R_dc
    P0 = i0**2 * R_dc

    OD = 2*(R_in + n_layers*d_wire)
    height = coil_height
    volume_cm3 = np.pi*(OD/2)**2*height*1e6

    # ---------- Approximate inductance (Wheeler multilayer) ----------
    r_mean = (ID + OD)/4          # mean radius [m]
    coil_width = (OD - ID)/2      # radial thickness [m]
    coil_length = height          # axial length [m]

    # convert to inches
    r_in = r_mean / 0.0254
    l_in = coil_length / 0.0254
    w_in = coil_width / 0.0254

    # Wheeler multilayer formula (µH)
    L_uH = (0.8 * r_in**2 * N**2) / (6*r_in + 9*l_in + 10*w_in)

    L = L_uH * 1e-6

    feasible = (
        (V0 <= V_max) and
        (i0 <= I_max) and
        (P0 <= P_max) and
        (weight_kg * 1000 <= W_max) and
        (total_length <= L_wire_max)
    )

    return dict(awg=awg, n_layers=n_layers, turns_per_layer=turns_per_layer,
                N=N, i0=i0, R_dc=R_dc, V0=V0, P0=P0, L=L, tau=L/R_dc if R_dc>0 else np.nan,
                weight_g=weight_kg*1000, OD_mm=OD*1000, height_mm=height*1000,
                volume_cm3=volume_cm3, length_m=total_length, feasible=feasible)

# ============================================================
# Sweep the design space
# ============================================================
print("Sweeping design space...")
designs = []
for awg in awg_range:
    for nl in n_layers_range:
        for tpl in turns_pl_range:
            d = build_design(awg, nl, tpl)
            if d is not None:
                designs.append(d)
print(f"Evaluated {len(designs)} candidate designs.")

feasible_designs = [d for d in designs if d['feasible']]
print(f"{len(feasible_designs)} are feasible under V<={V_max}V, I<={I_max}A, P<={P_max}W, W<={W_max}g, L<={L_wire_max}m")

scatter_lookup = feasible_designs
# ============================================================
# Pareto front: minimize (weight_g, P0), via sort + running-minimum sweep
# ============================================================
def pareto_front(points, x_key, y_key):
    """Non-dominated set for minimizing both x_key and y_key.
    O(n log n): sort by x ascending, keep a point only if it beats the
    best y seen so far among lighter/equal-x points -- this is exactly
    the definition of 'no other point is better-or-equal in both and
    strictly better in at least one'."""
    pts_sorted = sorted(points, key=lambda p: p[x_key])
    front = []
    best_y = np.inf
    for p in pts_sorted:
        if p[y_key] < best_y - 1e-12:
            front.append(p)
            best_y = p[y_key]
    return front

front = pareto_front(feasible_designs, 'weight_g', 'P0')
front_sorted = sorted(front, key=lambda p: p['weight_g'])

print(f"\n{len(front_sorted)} Pareto-optimal designs found (weight vs power).")
print("\n--- Pareto frontier (lightest -> heaviest) ---")
print(f"{'AWG':>4} {'layers':>7} {'t/layer':>8} {'N':>5} {'i0(A)':>7} "
      f"{'V(V)':>6} {'P(W)':>7} {'wt(g)':>7} {'len(m)':>7} {'OD(mm)':>7} {'h(mm)':>7}")
for p in front_sorted:
    print(f"{p['awg']:>4} {p['n_layers']:>7} {p['turns_per_layer']:>8} {p['N']:>5} "
          f"{p['i0']:>7.2f} {p['V0']:>6.2f} {p['P0']:>7.2f} {p['weight_g']:>7.1f} "
          f"{p['length_m']:>7.2f} {p['OD_mm']:>7.1f} {p['height_mm']:>7.1f}")




# ============================================================
# Plot: weight vs power, all feasible designs + Pareto front highlighted
# ============================================================
fig, axes = plt.subplots(1, 2, figsize=(13, 5.5))

# --- Left: weight vs power ---
ax = axes[0]
wt_all = [d['weight_g'] for d in feasible_designs]
p_all  = [d['P0'] for d in feasible_designs]
scatter1 = ax.scatter(
    wt_all,
    p_all,
    s=8,
    alpha=0.3,
    color='steelblue',
    picker=True,
)

wt_f = [p['weight_g'] for p in front_sorted]
p_f  = [p['P0'] for p in front_sorted]
ax.plot(wt_f, p_f, '-o', color='crimson', markersize=5, linewidth=1.8,
         label='Pareto front (non-dominated)')

ax.set_xlabel('Coil copper weight (g)')
ax.set_ylabel('Hold power P = i0^2 * R (W)')
ax.set_title('Weight vs Power tradeoff')
ax.set_yscale('log')
ax.grid(True, alpha=0.3, which='both')
ax.legend()

# --- Right: volume vs power ---
ax2 = axes[1]
vol_all = [d['volume_cm3'] for d in feasible_designs]
scatter2 = ax2.scatter(
    vol_all,
    p_all,
    s=8,
    alpha=0.3,
    color='seagreen',
    picker=True,
)
vol_f = [p['volume_cm3'] for p in front_sorted]
ax2.plot(vol_f, p_f, '-o', color='crimson', markersize=5, linewidth=1.8,
          label='Pareto front (weight-optimal)')
ax2.set_xlabel('Coil envelope volume (cm^3)')
ax2.set_ylabel('Hold power P (W)')
ax2.set_title('Volume vs Power (same Pareto set)')
ax2.set_yscale('log')
ax2.grid(True, alpha=0.3, which='both')
ax2.legend()

annotation1 = ax.annotate(
    "",
    xy=(0, 0),
    xytext=(20, 20),
    textcoords="offset points",
    bbox=dict(boxstyle="round", fc="white"),
    arrowprops=dict(arrowstyle="->"),
)
annotation1.set_visible(False)

annotation2 = ax2.annotate(
    "",
    xy=(0, 0),
    xytext=(20, 20),
    textcoords="offset points",
    bbox=dict(boxstyle="round", fc="white"),
    arrowprops=dict(arrowstyle="->"),
)
annotation2.set_visible(False)


def on_pick(event):

    ind = event.ind[0]
    d = scatter_lookup[ind]

    txt = (
        f"AWG {d['awg']}\n"
        f"{d['n_layers']} layers\n"
        f"{d['turns_per_layer']} turns/layer\n"
        f"N = {d['N']}\n"
        f"I = {d['i0']:.2f} A\n"
        f"V = {d['V0']:.2f} V\n"
        f"P = {d['P0']:.2f} W\n"
        f"R = {d['R_dc']:.3f} Ω\n"
        f"L = {d['L']*1000:.2f} mH\n"
        f"τ = {1000*d['tau']:.2f} ms\n"
        f"Weight = {d['weight_g']:.1f} g\n"
        f"Wire length = {d['length_m']:.2f} m\n"
        f"OD = {d['OD_mm']:.1f} mm\n"
        f"Height = {d['height_mm']:.1f} mm"
    )

    print("\n" + "="*50)
    print(txt)
    print("="*50)

    # Hide both annotations first
    annotation1.set_visible(False)
    annotation2.set_visible(False)

    if event.artist == scatter1:
        annotation = annotation1
        x = d['weight_g']
        y = d['P0']
    elif event.artist == scatter2:
        annotation = annotation2
        x = d['volume_cm3']
        y = d['P0']
    else:
        return

    annotation.xy = (x, y)
    annotation.set_text(txt)
    annotation.set_visible(True)

    fig.canvas.draw_idle()


fig.canvas.mpl_connect("pick_event", on_pick)


plt.tight_layout()
plt.show()

# ============================================================
# Pick a "knee" design: the point of diminishing returns on the front
# (max curvature point via simple discrete estimate) as a sane default
# ============================================================
if len(front_sorted) >= 3:
    w = np.array([p['weight_g'] for p in front_sorted])
    p = np.array([p['P0'] for p in front_sorted])
    # normalize both axes 0-1 so curvature isn't dominated by units
    wn = (w - w.min())/(w.max()-w.min()+1e-9)
    pn = (p - p.min())/(p.max()-p.min()+1e-9)
    dist_to_origin_corner = np.hypot(wn, pn)  # distance to the ideal (0 weight, 0 power) corner
    knee_idx = int(np.argmin(dist_to_origin_corner))
    knee = front_sorted[knee_idx]

    ax.scatter(
        knee['weight_g'],
        knee['P0'],
        s=180,
        marker='*',
        color='gold',
        edgecolor='black',
        linewidth=1.5,
        zorder=20,
        label='Chosen design'
    )

    ax2.scatter(
        knee['volume_cm3'],
        knee['P0'],
        s=180,
        marker='*',
        color='gold',
        edgecolor='black',
        linewidth=1.5,
        zorder=20,
    )

    print(f"\n--- Suggested 'knee' design (best balance of weight vs power) ---")
    print(f"AWG {knee['awg']}, {knee['n_layers']} layers x {knee['turns_per_layer']} turns/layer "
          f"= {knee['N']} turns")
    print(f"i0={knee['i0']:.2f} A, V={knee['V0']:.2f} V, P={knee['P0']:.2f} W, "
          f"weight={knee['weight_g']:.0f} g, OD={knee['OD_mm']:.1f} mm, height={knee['height_mm']:.1f} mm, length={knee['length_m']:.1f} m")
