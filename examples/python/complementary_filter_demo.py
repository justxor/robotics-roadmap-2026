"""Комплементарный фильтр vs «чистый» гироскоп vs «чистый» акселерометр.

Симулируем IMU, который качается по синусоиде:
  - акселерометр: правильный в среднем, но шумный (вибрации);
  - гироскоп: гладкий, но со смещением нуля (bias) -> интеграл уплывает.
Фильтр берёт лучшее от обоих.

Запуск:  pip install numpy matplotlib && python complementary_filter_demo.py
"""
import numpy as np


def simulate(duration=20.0, dt=0.005, alpha=0.98, seed=0):
    rng = np.random.default_rng(seed)
    t = np.arange(0, duration, dt)
    true_angle = 10 * np.sin(2 * np.pi * 0.3 * t)                     # °
    true_rate = np.gradient(true_angle, dt)                            # °/с

    acc_angle = true_angle + rng.normal(0, 3.0, t.size)                # шум ±3°
    gyro_rate = true_rate + 0.8 + rng.normal(0, 0.3, t.size)           # bias 0.8 °/с

    gyro_only = np.zeros_like(t)
    fused = np.zeros_like(t)
    gyro_only[0] = fused[0] = acc_angle[0]
    for k in range(1, t.size):
        gyro_only[k] = gyro_only[k - 1] + gyro_rate[k] * dt
        fused[k] = alpha * (fused[k - 1] + gyro_rate[k] * dt) + (1 - alpha) * acc_angle[k]

    return t, true_angle, acc_angle, gyro_only, fused


def rmse(a, b):
    return float(np.sqrt(np.mean((a - b) ** 2)))


def main():
    t, true, acc, gyro, fused = simulate()
    print(f"RMSE акселерометр:  {rmse(acc, true):6.2f}°  (шумно)")
    print(f"RMSE гироскоп:      {rmse(gyro, true):6.2f}°  (уплывает, к концу ошибка {gyro[-1] - true[-1]:.1f}°)")
    print(f"RMSE комплементарный:{rmse(fused, true):6.2f}°")

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib не установлен — пропускаю график")
        return
    plt.figure(figsize=(10, 5))
    plt.plot(t, acc, lw=0.5, alpha=0.5, label="акселерометр")
    plt.plot(t, gyro, label="интеграл гироскопа")
    plt.plot(t, fused, lw=2, label="комплементарный фильтр")
    plt.plot(t, true, "k--", label="истина")
    plt.xlabel("время, с"); plt.ylabel("угол, °"); plt.legend(); plt.grid(alpha=0.3)
    plt.title("Почему нужен фильтр")
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
