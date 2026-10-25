// NxGyroAim.cs -- прицеливание гироскопом Свитча.
//
// Камера ULTRAKILL копит обзор мышью в CameraController.rotationX (тангаж,
// вверх -- плюс) и rotationY (рыскание, вправо -- плюс), а в своём Update
// ограничивает и применяет их. Мы прибавляем поворот гироскопа к тем же полям
// РАНЬШЕ её Update (DefaultExecutionOrder), поэтому он попадает в тот же кадр
// и проходит через её же ограничения, как обычная мышь.
//
// Используем типизированные поля из подключённой сборки игры: без reflection
// и упаковки float/bool в объекты в каждом кадре.
//
// Угол уже умножен на gyro_sens и развёрнут по gyro_invert_x/y в загрузчике
// (config.txt рядом с .nro), здесь он прибавляется как есть.

using UnityEngine;

namespace NxPort
{
    [DefaultExecutionOrder(-1000)]
    public sealed class NxGyroAim : MonoBehaviour
    {
        NxbMotion _m;

        void Update()
        {
            // Забираем накопленное каждый кадр, даже если не применяем: иначе
            // поворот из меню прилетит в камеру разом при возврате в игру.
            bool got = NxBridge.Motion(ref _m);
            if (!got) return;

            var cc = global::CameraController.Instance;
            if (!cc || !cc.activated) return;
            cc.rotationY += _m.yaw;
            cc.rotationX += _m.pitch;
        }
    }
}
