"""Телеуправление дифференциальным роботом + одометрия — хост-часть к прошивке 07_diffdrive_bridge.

Что показывает:
  * кинематику дифф-привода: (v, ω) -> скорости колёс -> «тики за кадр PID»;
  * одометрию: тики энкодеров -> (x, y, θ) робота;
  * общение с МК по текстовому протоколу (e / m / r / u / v).
Это ровно то, что делает diff_drive_controller в ros2_control, только в 200 строках.

Режимы:
  python diffdrive_teleop.py --sim                 # без железа: встроенный симулятор прошивки
  python diffdrive_teleop.py --sim --demo square   # робот проезжает квадрат 1×1 м, печать одометрии
  python diffdrive_teleop.py --port /dev/ttyUSB0   # реальный робот (pip install pyserial)
                                                   # Windows: --port COM5, macOS: /dev/cu.usbserial-XXXX
Клавиши (в режиме телеуправления):  W/S — вперёд/назад, A/D — поворот, ПРОБЕЛ — стоп, Q — выход.
"""
import argparse
import math
import random
import sys
import time
from dataclasses import dataclass

# ---------------- параметры робота (ИЗМЕРЬ СВОИ) ----------------
WHEEL_RADIUS = 0.035        # м — радиус колеса (Pololu Romi: 0.035)
WHEEL_BASE = 0.141          # м — расстояние между центрами колёс (Romi: 0.141)
TICKS_PER_REV = 816         # тиков энкодера на оборот колеса (как в 02_motor_encoder)
PID_RATE = 30               # Гц — должно совпадать с прошивкой
MAX_LINEAR = 0.4            # м/с
MAX_ANGULAR = 3.0           # рад/с


# ---------------- кинематика ----------------
def twist_to_wheels(v, w):
    """(v м/с, ω рад/с) -> (v_left, v_right) м/с."""
    return v - w * WHEEL_BASE / 2, v + w * WHEEL_BASE / 2


def speed_to_ticks_per_frame(speed_mps):
    rev_per_s = speed_mps / (2 * math.pi * WHEEL_RADIUS)
    return round(rev_per_s * TICKS_PER_REV / PID_RATE)


@dataclass
class Odometry:
    x: float = 0.0
    y: float = 0.0
    theta: float = 0.0
    prev_l: int | None = None
    prev_r: int | None = None

    def update(self, ticks_l, ticks_r):
        if self.prev_l is None:
            self.prev_l, self.prev_r = ticks_l, ticks_r
            return
        m_per_tick = 2 * math.pi * WHEEL_RADIUS / TICKS_PER_REV
        dl = (ticks_l - self.prev_l) * m_per_tick
        dr = (ticks_r - self.prev_r) * m_per_tick
        self.prev_l, self.prev_r = ticks_l, ticks_r
        ds = (dl + dr) / 2
        dth = (dr - dl) / WHEEL_BASE
        # интегрируем по средней ориентации (точнее, чем по начальной)
        self.x += ds * math.cos(self.theta + dth / 2)
        self.y += ds * math.sin(self.theta + dth / 2)
        self.theta = math.atan2(math.sin(self.theta + dth), math.cos(self.theta + dth))

    def __str__(self):
        return f"x={self.x:+.3f} м  y={self.y:+.3f} м  θ={math.degrees(self.theta):+7.1f}°"


# ---------------- связь с прошивкой ----------------
class SerialRobot:
    def __init__(self, port, baud=57600):
        try:
            import serial  # pyserial
        except ImportError:
            sys.exit("Нужен pyserial:  pip install pyserial")
        self.ser = serial.Serial(port, baud, timeout=0.2)
        time.sleep(2.0)            # ESP32 перезагружается при открытии порта
        self.ser.reset_input_buffer()

    def cmd(self, line):
        self.ser.write((line + "\r").encode())
        return self.ser.readline().decode(errors="ignore").strip()

    def close(self):
        self.cmd("m 0 0")
        self.ser.close()


class SimRobot:
    """Эмулятор прошивки: та же логика PID (инкрементальная форма, Ko-делитель) + модель мотора.

    Модель мотора первого порядка: скорость стремится к k·ШИМ с постоянной времени tau,
    плюс мёртвая зона (трение покоя) и шум энкодера. Достаточно, чтобы увидеть, как работает PID.
    """

    def __init__(self, kp=200, kd=100, ki=0, ko=50, seed=1):
        self.kp, self.kd, self.ki, self.ko = kp, kd, ki, ko
        self.enc = [0.0, 0.0]
        self.speed = [0.0, 0.0]               # тиков за кадр
        self.target = [0, 0]
        self.pwm = [0, 0]
        self.prev_input = [0, 0]
        self.iterm = [0, 0]
        self.moving = False
        self.rng = random.Random(seed)
        self.gain = [0.040, 0.036]            # колёса чуть разные — как в жизни
        self.last_t = time.monotonic()

    def _motor_step(self, i):
        pwm = self.pwm[i]
        drive = 0.0 if abs(pwm) < 120 else self.gain[i] * pwm      # мёртвая зона
        self.speed[i] += (drive - self.speed[i]) * 0.35             # инерция
        self.enc[i] += self.speed[i] + self.rng.gauss(0, 0.3)

    def _pid_step(self, i):
        prev_enc = self.enc[i]
        self._motor_step(i)
        if not self.moving:
            return
        inp = round(self.enc[i] - prev_enc)
        err = self.target[i] - inp
        out = int((self.kp * err - self.kd * (inp - self.prev_input[i]) + self.iterm[i]) / self.ko)  # как в C: усечение к нулю
        out += self.pwm[i]
        if out >= 1023:
            out = 1023
        elif out <= -1023:
            out = -1023
        else:
            self.iterm[i] += self.ki * err
        self.pwm[i] = int(out)
        self.prev_input[i] = inp

    def advance(self, seconds):
        for _ in range(max(1, round(seconds * PID_RATE))):
            self._pid_step(0)
            self._pid_step(1)

    def _sync_time(self):
        now = time.monotonic()
        self.advance(now - self.last_t)
        self.last_t = now

    def cmd(self, line):
        if not getattr(self, "manual_clock", False):
            self._sync_time()
        parts = line.split()
        c = parts[0]
        if c == "e":
            return f"{round(self.enc[0])} {round(self.enc[1])}"
        if c == "r":
            self.enc = [0.0, 0.0]
            return "OK"
        if c == "m":
            self.target = [int(parts[1]), int(parts[2])]
            self.moving = self.target != [0, 0]
            if not self.moving:
                self.pwm = [0, 0]
                self.iterm = [0, 0]
                self.prev_input = [0, 0]
            return "OK"
        if c == "u":
            self.kp, self.kd, self.ki, self.ko = map(int, parts[1].split(":"))
            return "OK"
        if c == "v":
            return "7400"
        return "Invalid Command"

    def close(self):
        pass


# ---------------- высокоуровневое управление ----------------
class Driver:
    def __init__(self, robot):
        self.robot = robot
        self.odom = Odometry()
        self.robot.cmd("r")

    def send_twist(self, v, w):
        v = max(-MAX_LINEAR, min(MAX_LINEAR, v))
        w = max(-MAX_ANGULAR, min(MAX_ANGULAR, w))
        vl, vr = twist_to_wheels(v, w)
        self.robot.cmd(f"m {speed_to_ticks_per_frame(vl)} {speed_to_ticks_per_frame(vr)}")

    def poll(self):
        reply = self.robot.cmd("e").split()
        if len(reply) == 2:
            self.odom.update(int(reply[0]), int(reply[1]))
        return self.odom


def run_demo_square(robot, side=1.0, v=0.25, w=1.2, realtime=False):
    """Квадрат по одометрии: едем до side метров, поворачиваем на 90° — 4 раза."""
    drv = Driver(robot)
    dt = 1.0 / 20
    sim = isinstance(robot, SimRobot)
    if sim and not realtime:
        robot.manual_clock = True      # ускоренная симуляция: время двигаем сами

    def tick():
        if sim and not realtime:
            robot.advance(dt)
        else:
            time.sleep(dt)
        return drv.poll()

    drv.poll()
    corners = [(side, 0), (side, side), (0, side), (0, 0)]
    for k, (gx, gy) in enumerate(corners):
        heading = k * math.pi / 2                      # курс на эту сторону (абсолютный)
        # 1) едем к углу: P-регулятор по оставшейся дистанции + удержание курса
        while True:
            remaining = (gx - drv.odom.x) * math.cos(heading) + (gy - drv.odom.y) * math.sin(heading)
            if remaining < 0.01:
                break
            yaw_err = math.atan2(math.sin(heading - drv.odom.theta), math.cos(heading - drv.odom.theta))
            drv.send_twist(min(v, max(0.05, 1.5 * remaining)), 3.0 * yaw_err)
            tick()
        # 2) поворот на следующий курс, тоже с замедлением у цели
        target = heading + math.pi / 2
        while True:
            yaw_err = math.atan2(math.sin(target - drv.odom.theta), math.cos(target - drv.odom.theta))
            if abs(yaw_err) < 0.02:
                break
            drv.send_twist(0, math.copysign(min(w, max(0.4, 3.0 * abs(yaw_err))), yaw_err))
            tick()
        drv.send_twist(0, 0)
        for _ in range(5):
            tick()
        print(f"сторона {k + 1}: {drv.odom}")
    drv.send_twist(0, 0)
    err = math.hypot(drv.odom.x, drv.odom.y)
    print(f"Вернулись в старт с ошибкой {err * 100:.1f} см (по одометрии)")
    print("На реальном роботе измерь рулеткой, где он остановился, — разница и есть дрейф одометрии.")
    return drv.odom


def run_teleop(robot):
    import curses

    def loop(scr):
        scr.nodelay(True)
        drv = Driver(robot)
        v = w = 0.0
        last_key = time.monotonic()
        while True:
            ch = scr.getch()
            if ch != -1:
                last_key = time.monotonic()
                key = chr(ch).lower() if 0 <= ch < 256 else ""
                if key == "q":
                    break
                if key == "w":
                    v = min(v + 0.05, MAX_LINEAR)
                elif key == "s":
                    v = max(v - 0.05, -MAX_LINEAR)
                elif key == "a":
                    w = min(w + 0.3, MAX_ANGULAR)
                elif key == "d":
                    w = max(w - 0.3, -MAX_ANGULAR)
                elif key == " ":
                    v = w = 0.0
            if time.monotonic() - last_key > 5:       # «мёртвая рука»: 5 с без клавиш — стоп
                v = w = 0.0
            drv.send_twist(v, w)
            odom = drv.poll()
            scr.erase()
            scr.addstr(0, 0, "W/S — скорость, A/D — поворот, ПРОБЕЛ — стоп, Q — выход")
            scr.addstr(2, 0, f"команда: v={v:+.2f} м/с  ω={w:+.2f} рад/с")
            scr.addstr(3, 0, f"одометрия: {odom}")
            scr.refresh()
            time.sleep(0.05)
        drv.send_twist(0, 0)

    curses.wrapper(loop)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--sim", action="store_true", help="встроенный симулятор вместо железа")
    src.add_argument("--port", help="последовательный порт робота")
    ap.add_argument("--demo", choices=["square"], help="автоматическая демонстрация вместо телеуправления")
    args = ap.parse_args()

    robot = SimRobot() if args.sim else SerialRobot(args.port)
    try:
        if args.demo == "square":
            run_demo_square(robot, realtime=not args.sim)
        else:
            run_teleop(robot)
    finally:
        robot.close()


if __name__ == "__main__":
    main()
