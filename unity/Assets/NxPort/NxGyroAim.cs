// NxGyroAim.cs -- прицеливание гироскопом Свитча.
//
// Камера ULTRAKILL копит обзор мышью в CameraController.rotationX (тангаж,
// вверх -- плюс) и rotationY (рыскание, вправо -- плюс), а в своём Update
// ограничивает и применяет их. Мы прибавляем поворот гироскопа к тем же полям
// РАНЬШЕ её Update (DefaultExecutionOrder), поэтому он попадает в тот же кадр
// и проходит через её же ограничения, как обычная мышь.
//
// Поля берём отражением: код игры подключён в проект готовой сборкой, и
// ссылаться на неё из asmdef не нужно. Имена сверены с открытыми модами
// (CameraController.Instance, rotationX/rotationY, activated).
//
// Угол уже умножен на gyro_sens и развёрнут по gyro_invert_x/y в загрузчике
// (config.txt рядом с .nro), здесь он прибавляется как есть.

using System;
using System.Reflection;
using UnityEngine;

namespace NxPort
{
    [DefaultExecutionOrder(-1000)]
    public sealed class NxGyroAim : MonoBehaviour
    {
        Type _ccType;
        PropertyInfo _instance;
        FieldInfo _rotX, _rotY, _activated;
        bool _bound, _failed;
        NxbMotion _m;

        bool Bind()
        {
            if (_bound) return true;
            if (_failed) return false;

            _ccType = Type.GetType("CameraController, Assembly-CSharp");
            const BindingFlags inst = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            const BindingFlags stat = BindingFlags.Static | BindingFlags.Public | BindingFlags.FlattenHierarchy;
            if (_ccType != null)
            {
                _instance = _ccType.GetProperty("Instance", stat);
                _rotX = _ccType.GetField("rotationX", inst);
                _rotY = _ccType.GetField("rotationY", inst);
                _activated = _ccType.GetField("activated", inst);
            }

            if (_ccType == null || _instance == null || _rotX == null || _rotY == null)
            {
                _failed = true;
                NxBridge.Log("гироприцел выключен: не найдено " +
                             (_ccType == null ? "CameraController" :
                              _instance == null ? "CameraController.Instance" :
                              _rotX == null ? "rotationX" : "rotationY"));
                return false;
            }

            _bound = true;
            NxBridge.Log("гироприцел привязан к CameraController" +
                         (_activated == null ? " (без флага activated)" : ""));
            return true;
        }

        void Update()
        {
            // Забираем накопленное каждый кадр, даже если не применяем: иначе
            // поворот из меню прилетит в камеру разом при возврате в игру.
            bool got = NxBridge.Motion(ref _m);
            if (!got || !Bind()) return;

            object cc = _instance.GetValue(null);
            if (cc == null || (cc as UnityEngine.Object) == null) return;
            if (_activated != null && !(bool)_activated.GetValue(cc)) return;

            _rotY.SetValue(cc, (float)_rotY.GetValue(cc) + _m.yaw);
            _rotX.SetValue(cc, (float)_rotX.GetValue(cc) + _m.pitch);
        }
    }
}
