"""LQR vs PID для перевёрнутого маятника на тележке (cart-pole).

Модель линеаризована около верхнего положения. Симуляция — нелинейная.
В момент t = 2 c по маятнику «бьём» импульсом (толчок) и смотрим, кто лучше справится.
PID управляет только углом (тележка уезжает), LQR — всем состоянием [x, x', θ, θ'].

Запуск:  pip install numpy scipy matplotlib && python cartpole_lqr.py
"""
import numpy as np
from scipy.linalg import solve_continuous_are

# Параметры
M, m, L, g = 1.0, 0.2, 0.5, 9.81   # масса тележки, маятника, длина до ЦМ, g
DT, T_END = 0.002, 8.0
U_MAX = 20.0                       # Н — ограничение силы


def dynamics(s, u):
    """Нелинейная модель. s = [x, x_dot, th, th_dot], th = 0 — вертикально вверх."""
    x, xd, th, thd = s
    sin, cos = np.sin(th), np.cos(th)
    denom = M + m * sin ** 2
    xdd = (u + m * sin * (L * thd ** 2 - g * cos)) / denom
    thdd = (-u * cos - m * L * thd ** 2 * sin * cos + (M + m) * g * sin) / (L * denom)
    return np.array([xd, xdd, thd, thdd])


def lqr_gain():
    A = np.array([[0, 1, 0, 0],
                  [0, 0, -m * g / M, 0],
                  [0, 0, 0, 1],
                  [0, 0, (M + m) * g / (M * L), 0]])
    B = np.array([[0], [1 / M], [0], [-1 / (M * L)]])
    Q = np.diag([10, 1, 100, 1])
    R = np.array([[0.1]])
    P = solve_continuous_are(A, B, Q, R)
    return np.linalg.inv(R) @ B.T @ P   # u = -K s


def run(controller):
    s = np.array([0.0, 0.0, np.deg2rad(5), 0.0])
    integ, log = 0.0, []
    for k in range(int(T_END / DT)):
        t = k * DT
        if abs(t - 2.0) < DT / 2:
            s[3] += 1.5                                   # толчок по маятнику
        u, integ = controller(s, integ)
        u = float(np.clip(u, -U_MAX, U_MAX))
        s = s + dynamics(s, u) * DT                       # Эйлер — для учебного примера достаточно
        log.append((t, s[0], np.rad2deg(s[2]), u))
        if abs(s[2]) > np.pi / 2:
            break                                         # упал
    return np.array(log)


K = lqr_gain()


def lqr_ctrl(s, integ):
    return float(-(K @ s)[0]), integ


def pid_ctrl(s, integ, kp=60.0, ki=20.0, kd=8.0):
    integ += s[2] * DT
    return kp * s[2] + ki * integ + kd * s[3], integ


def summarize(name, log):
    fell = abs(log[-1, 2]) > 89
    print(f"{name:4s}: {'УПАЛ' if fell else 'устоял'} | макс |θ| = {np.max(np.abs(log[:, 2])):5.1f}° | "
          f"тележка уехала на {log[-1, 1]:+6.2f} м | макс |u| = {np.max(np.abs(log[:, 3])):5.1f} Н")
    return fell


def main():
    print("K (LQR) =", np.round(K, 2))
    lqr_log, pid_log = run(lqr_ctrl), run(pid_ctrl)
    summarize("LQR", lqr_log)
    summarize("PID", pid_log)
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        return
    fig, ax = plt.subplots(3, 1, sharex=True, figsize=(9, 7))
    for log, name in [(lqr_log, "LQR"), (pid_log, "PID")]:
        ax[0].plot(log[:, 0], log[:, 2], label=name)
        ax[1].plot(log[:, 0], log[:, 1], label=name)
        ax[2].plot(log[:, 0], log[:, 3], label=name)
    for a, lab in zip(ax, ["θ, °", "x тележки, м", "сила u, Н"]):
        a.set_ylabel(lab); a.grid(alpha=0.3); a.legend()
    ax[2].set_xlabel("время, с")
    fig.suptitle("Cart-pole: толчок в t = 2 с")
    plt.tight_layout(); plt.show()


if __name__ == "__main__":
    main()
