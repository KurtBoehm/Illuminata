import numpy as np


def mat(msg: str):
    l = [float(v) for v in msg.split()]
    return np.array([l[::2], l[1::2]])


def rotate_mat(angle: float):
    theta = np.radians(angle)
    c, s = np.cos(theta), np.sin(theta)
    mat = np.eye(3, 3)
    mat[:2, :2] = np.array(((c, -s), (s, c)))
    return mat


def translate_mat(x: float, y: float):
    mat = np.eye(3, 3)
    mat[0, 2] = x
    mat[1, 2] = y
    return mat
