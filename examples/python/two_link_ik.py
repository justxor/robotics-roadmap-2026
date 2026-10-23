"""Прямая и обратная кинематика двухзвенного плоского манипулятора.

       (x, y)  ← схват
        /
   L2  /  θ2
      o
     /
 L1 /  θ1
   o──────── x   (основание)

Прямая:   x = L1 cos θ1 + L2 cos(θ1+θ2),   y = L1 sin θ1 + L2 sin(θ1+θ2)
Обратная: по теореме косинусов (два решения — «локоть вверх» и «локоть вниз»).

Запуск:  pip install numpy matplotlib && python two_link_ik.py
"""
import numpy as np

L1, L2 = 0.12, 0.10   # м — примерно как у маленькой руки


def forward(th1, th2):
    x = L1 * np.cos(th1) + L2 * np.cos(th1 + th2)
    y = L1 * np.sin(th1) + L2 * np.sin(th1 + th2)
    return np.array([x, y])


def inverse(x, y, elbow_up=True):
    r2 = x * x + y * y
    c2 = (r2 - L1 ** 2 - L2 ** 2) / (2 * L1 * L2)
    if abs(c2) > 1:
        raise ValueError(f"Точка ({x:.3f}, {y:.3f}) вне рабочей зоны")
    s2 = np.sqrt(1 - c2 ** 2) * (-1 if elbow_up else 1)
    th2 = np.arctan2(s2, c2)
    th1 = np.arctan2(y, x) - np.arctan2(L2 * s2, L1 + L2 * c2)
    return th1, th2


def main():
    targets = [(0.15, 0.08), (0.05, 0.18), (0.20, 0.0), (-0.1, 0.12)]
    for x, y in targets:
        for elbow in (True, False):
            th1, th2 = inverse(x, y, elbow)
            err = np.linalg.norm(forward(th1, th2) - [x, y])
            print(f"цель ({x:+.2f},{y:+.2f}) локоть {'вверх' if elbow else 'вниз '}: "
                  f"θ1={np.degrees(th1):7.2f}°, θ2={np.degrees(th2):7.2f}°, ошибка FK={err:.1e} м")
    try:
        inverse(0.3, 0.0)
    except ValueError as e:
        print("Ожидаемо:", e)

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        return
    # Рисуем руку, проходящую по окружности
    fig, ax = plt.subplots(figsize=(6, 6))
    for a in np.linspace(0, 2 * np.pi, 16, endpoint=False):
        x, y = 0.13 + 0.05 * np.cos(a), 0.08 + 0.05 * np.sin(a)
        th1, th2 = inverse(x, y)
        elbow = [L1 * np.cos(th1), L1 * np.sin(th1)]
        ax.plot([0, elbow[0], x], [0, elbow[1], y], "o-", alpha=0.5)
    ax.set_aspect("equal"); ax.grid(alpha=0.3); ax.set_title("IK: схват идёт по окружности")
    plt.show()


if __name__ == "__main__":
    main()
