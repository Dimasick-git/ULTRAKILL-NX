using System.Collections.Generic;
using System.Reflection;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.EventSystems;

namespace NxPort
{
    // Dispatch alongside the original navigation module, never replace it.
    public sealed class NxTouchModule : PointerInputModule
    {
        readonly List<RaycastResult> hits = new List<RaycastResult>();
        PointerEventData pointer;
        bool held;
        Vector2 start;
        BaseRaycaster pressedRaycaster;
        Component pressedTerminal;
        static readonly System.Type TerminalType = typeof(ShopButton).Assembly.GetType("ControllerPointer");
        static readonly PropertyInfo TerminalPressed = TerminalType?.GetProperty("OnPressed", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);

        public static UnityEvent TerminalPressEvent(object terminal) =>
            terminal != null && TerminalType != null && TerminalType.IsInstanceOfType(terminal)
                ? TerminalPressed?.GetValue(terminal) as UnityEvent : null;

        static Component Terminal(GameObject target)
        {
            var terminal = target && TerminalType != null ? target.GetComponentInParent(TerminalType) as Behaviour : null;
            return terminal && terminal.isActiveAndEnabled ? terminal : null;
        }

        static GameObject ClickTarget(GameObject target)
        {
            var click = ExecuteEvents.GetEventHandler<IPointerClickHandler>(target);
            var terminal = Terminal(target);
            return click ? click : terminal ? terminal.gameObject : null;
        }

        public override bool IsModuleSupported() => false;
        public override bool ShouldActivateModule() => false;
        public override void Process() { }

        public static Vector2 MapPoint(Vector2 screen, BaseRaycaster raycaster)
        {
            var camera = raycaster ? raycaster.eventCamera : null;
            if (!camera || !camera.targetTexture) return screen;
            return new Vector2(screen.x / Screen.width * camera.targetTexture.width,
                screen.y / Screen.height * camera.targetTexture.height);
        }

        RaycastResult Hit(Vector2 screen)
        {
            RaycastResult best = default;
            var raycasters = RaycasterManager.GetRaycasters();
            for (int i = 0; i < raycasters.Count; i++)
            {
                var caster = raycasters[i];
                if (!caster || !caster.isActiveAndEnabled) continue;
                pointer.position = MapPoint(screen, caster);
                hits.Clear(); caster.Raycast(pointer, hits);
                foreach (var hit in hits)
                {
                    if (!hit.gameObject) continue;
                    if (!best.gameObject || Above(hit, best)) best = hit;
                }
            }
            return best;
        }

        static bool Above(RaycastResult a, RaycastResult b)
        {
            if (a.module.sortOrderPriority != b.module.sortOrderPriority)
                return a.module.sortOrderPriority > b.module.sortOrderPriority;
            if (a.module.renderOrderPriority != b.module.renderOrderPriority)
                return a.module.renderOrderPriority > b.module.renderOrderPriority;
            int al = SortingLayer.GetLayerValueFromID(a.sortingLayer), bl = SortingLayer.GetLayerValueFromID(b.sortingLayer);
            if (al != bl) return al > bl;
            if (a.sortingOrder != b.sortingOrder) return a.sortingOrder > b.sortingOrder;
            if (a.depth != b.depth && a.module.rootRaycaster == b.module.rootRaycaster) return a.depth > b.depth;
            return a.distance < b.distance;
        }

        public void Sample(bool down, Vector2 screen)
        {
            if (!down && !held) return;
            if (pointer == null) pointer = new PointerEventData(eventSystem) { pointerId = 10000, button = PointerEventData.InputButton.Left };
            var previous = pointer.position;
            var hit = Hit(screen);
            var position = MapPoint(screen, held ? pressedRaycaster : hit.module);
            pointer.delta = held ? position - previous : Vector2.zero;
            pointer.position = position;
            pointer.pointerCurrentRaycast = hit;
            HandlePointerExitAndEnter(pointer, hit.gameObject);
            if (down && !held)
            {
                held = true; start = screen; pressedRaycaster = hit.module;
                pressedTerminal = Terminal(hit.gameObject);
                pointer.eligibleForClick = true; pointer.dragging = false; pointer.useDragThreshold = true;
                pointer.pressPosition = position; pointer.pointerPressRaycast = hit;
                pointer.clickTime = Time.unscaledTime; pointer.clickCount = 1;
                if (!NxSoftKeyboard.ContainsTarget(hit.gameObject)) DeselectIfSelectionChanged(hit.gameObject, pointer);
                pointer.pointerPress = ExecuteEvents.ExecuteHierarchy(hit.gameObject, pointer, ExecuteEvents.pointerDownHandler)
                    ?? ClickTarget(hit.gameObject);
                pointer.rawPointerPress = hit.gameObject;
                pointer.pointerDrag = ExecuteEvents.GetEventHandler<IDragHandler>(hit.gameObject);
                if (pointer.pointerDrag) ExecuteEvents.Execute(pointer.pointerDrag, pointer, ExecuteEvents.initializePotentialDrag);
                return;
            }
            if (down)
            {
                if (pointer.pointerDrag && !pointer.dragging && (!pointer.useDragThreshold ||
                    (screen - start).sqrMagnitude >= eventSystem.pixelDragThreshold * eventSystem.pixelDragThreshold))
                {
                    ExecuteEvents.Execute(pointer.pointerDrag, pointer, ExecuteEvents.beginDragHandler);
                    pointer.dragging = true;
                    if (pointer.pointerPress != pointer.pointerDrag)
                    {
                        ExecuteEvents.Execute(pointer.pointerPress, pointer, ExecuteEvents.pointerUpHandler);
                        pointer.eligibleForClick = false; pointer.pointerPress = null; pointer.rawPointerPress = null;
                    }
                }
                if (pointer.dragging) ExecuteEvents.Execute(pointer.pointerDrag, pointer, ExecuteEvents.dragHandler);
            }
            else Release(true);
        }

        void Release(bool click)
        {
            if (pointer == null) return;
            ExecuteEvents.Execute(pointer.pointerPress, pointer, ExecuteEvents.pointerUpHandler);
            var target = pointer.pointerCurrentRaycast.gameObject;
            if (click && pointer.eligibleForClick && pointer.pointerPress && pointer.pointerPress == ClickTarget(target))
            {
                var terminalEvent = pressedTerminal && pressedTerminal == Terminal(target)
                    ? TerminalPressEvent(pressedTerminal) : null;
                ExecuteEvents.Execute(pointer.pointerPress, pointer, ExecuteEvents.pointerClickHandler);
                // ShopButton subscribes to this event, not IPointerClickHandler.
                // Match ControllerPointer's native click + OnPressed dispatch.
                terminalEvent?.Invoke();
            }
            else if (click && pointer.dragging) ExecuteEvents.ExecuteHierarchy(target, pointer, ExecuteEvents.dropHandler);
            if (pointer.dragging) ExecuteEvents.Execute(pointer.pointerDrag, pointer, ExecuteEvents.endDragHandler);
            pointer.eligibleForClick = false; pointer.dragging = false; pointer.pointerPress = null;
            pointer.rawPointerPress = null; pointer.pointerDrag = null;
            HandlePointerExitAndEnter(pointer, null);
            pressedRaycaster = null; held = false;
            pressedTerminal = null;
        }

        public void Cancel() { Release(false); }
        protected override void OnDisable() { Cancel(); base.OnDisable(); }
    }
}
