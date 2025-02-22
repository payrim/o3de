/*
 * Copyright (c) Contributors to the Open 3D Engine Project. For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * 
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ViewportManipulatorController.h"

#include <AzToolsFramework/Manipulators/ManipulatorManager.h>
#include <AzToolsFramework/ViewportSelection/EditorInteractionSystemViewportSelectionRequestBus.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzFramework/Viewport/ScreenGeometry.h>
#include <AzCore/Script/ScriptTimePoint.h>

#include <QApplication>

static const auto ManipulatorPriority = AzFramework::ViewportControllerPriority::Highest;
static const auto InteractionPriority = AzFramework::ViewportControllerPriority::High;

namespace SandboxEditor
{
    ViewportManipulatorControllerInstance::ViewportManipulatorControllerInstance(
        AzFramework::ViewportId viewport, ViewportManipulatorController* controller)
        : AzFramework::MultiViewportControllerInstanceInterface<ViewportManipulatorController>(viewport, controller)
    {
    }

    AzToolsFramework::ViewportInteraction::MouseButton ViewportManipulatorControllerInstance::GetMouseButton(
        const AzFramework::InputChannel& inputChannel)
    {
        using AzToolsFramework::ViewportInteraction::MouseButton;
        using InputButton = AzFramework::InputDeviceMouse::Button;
        const auto& id = inputChannel.GetInputChannelId();
        if (id == InputButton::Left)
        {
            return MouseButton::Left;
        }
        if (id == InputButton::Middle)
        {
            return MouseButton::Middle;
        }
        if (id == InputButton::Right)
        {
            return MouseButton::Right;
        }
        return MouseButton::None;
    }

    bool ViewportManipulatorControllerInstance::IsMouseMove(const AzFramework::InputChannel& inputChannel)
    {
        return inputChannel.GetInputChannelId() == AzFramework::InputDeviceMouse::SystemCursorPosition;
    }

    AzToolsFramework::ViewportInteraction::KeyboardModifier ViewportManipulatorControllerInstance::GetKeyboardModifier(
        const AzFramework::InputChannel& inputChannel)
    {
        using AzToolsFramework::ViewportInteraction::KeyboardModifier;
        using Key = AzFramework::InputDeviceKeyboard::Key;
        const auto& id = inputChannel.GetInputChannelId();
        if (id == Key::ModifierAltL || id == Key::ModifierAltR)
        {
            return KeyboardModifier::Alt;
        }
        if (id == Key::ModifierCtrlL || id == Key::ModifierCtrlR)
        {
            return KeyboardModifier::Ctrl;
        }
        if (id == Key::ModifierShiftL || id == Key::ModifierShiftR)
        {
            return KeyboardModifier::Shift;
        }
        return KeyboardModifier::None;
    }

    bool ViewportManipulatorControllerInstance::HandleInputChannelEvent(const AzFramework::ViewportControllerInputEvent& event)
    {
        // We only care about manipulator and viewport interaction events
        if (event.m_priority != ManipulatorPriority && event.m_priority != InteractionPriority)
        {
            return false;
        }

        using InteractionBus = AzToolsFramework::EditorInteractionSystemViewportSelectionRequestBus;
        using namespace AzToolsFramework::ViewportInteraction;
        using AzFramework::InputChannel;

        bool interactionHandled = false;
        float wheelDelta = 0.0f;
        AZStd::optional<MouseButton> overrideButton;
        AZStd::optional<MouseEvent> eventType;

        // Because we receive events multiple times at separate priorities for manipulator events and
        // viewport interaction events, we want to avoid updating our "last tick state" until we're on our last event,
        // which currently is the low priority Interaction processor.
        const bool finishedProcessingEvents = event.m_priority == InteractionPriority;

        const auto state = event.m_inputChannel.GetState();
        if (IsMouseMove(event.m_inputChannel))
        {
            // Cache the ray trace results when doing manipulator interaction checks, no need to recalculate after
            if (event.m_priority == ManipulatorPriority)
            {
                AzFramework::ScreenPoint screenPosition = AzFramework::ScreenPoint(0, 0);
                ViewportMouseCursorRequestBus::EventResult(
                    screenPosition, GetViewportId(), &ViewportMouseCursorRequestBus::Events::ViewportCursorScreenPosition);

                m_mouseInteraction.m_mousePick.m_screenCoordinates = screenPosition;
                AZStd::optional<ProjectedViewportRay> ray;
                ViewportInteractionRequestBus::EventResult(
                    ray, GetViewportId(), &ViewportInteractionRequestBus::Events::ViewportScreenToWorldRay, screenPosition);

                if (ray.has_value())
                {
                    m_mouseInteraction.m_mousePick.m_rayOrigin = ray.value().origin;
                    m_mouseInteraction.m_mousePick.m_rayDirection = ray.value().direction;
                }
            }
            eventType = MouseEvent::Move;
        }
        else if (auto mouseButton = GetMouseButton(event.m_inputChannel); mouseButton != MouseButton::None)
        {
            const AZ::u32 mouseButtonValue = static_cast<AZ::u32>(mouseButton);
            overrideButton = mouseButton;
            if (state == InputChannel::State::Began)
            {
                m_mouseInteraction.m_mouseButtons.m_mouseButtons |= mouseButtonValue;
                if (IsDoubleClick(mouseButton))
                {
                    // Only remove the double click flag once we're done processing both Manipulator and Interaction events
                    if (event.m_priority == InteractionPriority)
                    {
                        m_pendingDoubleClicks.erase(mouseButton);
                    }
                    eventType = MouseEvent::DoubleClick;
                }
                else
                {
                    // Only insert the double click timing once we're done processing events, to avoid a false IsDoubleClick positive
                    if (finishedProcessingEvents)
                    {
                        m_pendingDoubleClicks[mouseButton] = m_curTime;
                    }
                    eventType = MouseEvent::Down;
                }
            }
            else if (state == InputChannel::State::Ended)
            {
                // If we've actually logged a mouse down event, forward a mouse up event.
                // This prevents corner cases like the context menu thinking it should be opened even though no one clicked in this viewport,
                // due to RenderViewportWidget ensuring all controllers get InputChannel::State::Ended events.
                if (m_mouseInteraction.m_mouseButtons.m_mouseButtons & mouseButtonValue)
                {
                    // Erase the button from our state if we're done processing events.
                    if (event.m_priority == InteractionPriority)
                    {
                        m_mouseInteraction.m_mouseButtons.m_mouseButtons &= ~mouseButtonValue;
                    }
                    eventType = MouseEvent::Up;
                }
            }
        }
        else if (auto keyboardModifier = GetKeyboardModifier(event.m_inputChannel); keyboardModifier != KeyboardModifier::None)
        {
            if (state == InputChannel::State::Began || state == InputChannel::State::Updated)
            {
                m_mouseInteraction.m_keyboardModifiers.m_keyModifiers |= static_cast<AZ::u32>(keyboardModifier);
            }
            else if (state == InputChannel::State::Ended)
            {
                m_mouseInteraction.m_keyboardModifiers.m_keyModifiers &= ~static_cast<AZ::u32>(keyboardModifier);
            }
        }
        else if (event.m_inputChannel.GetInputChannelId() == AzFramework::InputDeviceMouse::Movement::Z)
        {
            if (state == InputChannel::State::Began || state == InputChannel::State::Updated)
            {
                eventType = MouseEvent::Wheel;
                wheelDelta = event.m_inputChannel.GetValue();
            }
        }

        if (eventType)
        {
            MouseInteraction mouseInteraction = m_mouseInteraction;
            if (overrideButton)
            {
                mouseInteraction.m_mouseButtons.m_mouseButtons = static_cast<AZ::u32>(overrideButton.value());
            }

            mouseInteraction.m_interactionId.m_viewportId = GetViewportId();

            // Depending on priority, we dispatch to either the manipulator or viewport interaction event
            const auto& targetInteractionEvent = event.m_priority == ManipulatorPriority
                ? &InteractionBus::Events::InternalHandleMouseManipulatorInteraction
                : &InteractionBus::Events::InternalHandleMouseViewportInteraction;

            const auto mouseInteractionEvent = [mouseInteraction, event = eventType.value(), wheelDelta] {
                switch (event)
                {
                case MouseEvent::Up:
                case MouseEvent::Down:
                case MouseEvent::Move:
                case MouseEvent::DoubleClick:
                    return MouseInteractionEvent(AZStd::move(mouseInteraction), event);
                case MouseEvent::Wheel:
                    return MouseInteractionEvent(AZStd::move(mouseInteraction), wheelDelta);
                }

                AZ_Assert(false, "Unhandled MouseEvent");
                return MouseInteractionEvent(MouseInteraction{}, MouseEvent::Up);
            }();

            InteractionBus::EventResult(
                interactionHandled, AzToolsFramework::GetEntityContextId(), targetInteractionEvent, mouseInteractionEvent);
        }

        // Only filter button/key press events, not release events
        return interactionHandled && event.m_inputChannel.IsActive();
    }

    void ViewportManipulatorControllerInstance::ResetInputChannels()
    {
        m_pendingDoubleClicks.clear();
        m_mouseInteraction = AzToolsFramework::ViewportInteraction::MouseInteraction();
    }

    void ViewportManipulatorControllerInstance::UpdateViewport(const AzFramework::ViewportControllerUpdateEvent& event)
    {
        m_curTime = event.m_time;
    }

    bool ViewportManipulatorControllerInstance::IsDoubleClick(AzToolsFramework::ViewportInteraction::MouseButton button) const
    {
        auto clickIt = m_pendingDoubleClicks.find(button);
        if (clickIt == m_pendingDoubleClicks.end())
        {
            return false;
        }
        const double doubleClickThresholdMilliseconds = qApp->doubleClickInterval();
        return (m_curTime.GetMilliseconds() - clickIt->second.GetMilliseconds()) < doubleClickThresholdMilliseconds;
    }
} //namespace SandboxEditor
