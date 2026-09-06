#pragma once
#include <stdint.h>
#include "button_debounce.h"

enum class KeyEvent { None, Short, Long };
struct KeyTracker {
  ButtonDebounce debounce;
  bool wasHigh = true;
  bool longSent = false;
  uint32_t pressedAt = 0;
  KeyEvent update(bool high, uint32_t now) {
    debounce.update(high, now);
    const bool stable = debounce.stableHigh;
    KeyEvent event = KeyEvent::None;
    if (wasHigh && !stable) { pressedAt = now; longSent = false; }
    if (!wasHigh && stable && !longSent) event = KeyEvent::Short;
    if (!stable && !longSent && uint32_t(now - pressedAt) >= 800) {
      longSent = true; event = KeyEvent::Long;
    }
    wasHigh = stable;
    return event;
  }
};

enum class Screen { Home, Inbox, Actions, ConfirmMessage, ConfirmAll };
enum class UiKey { Up, Down, Enter, Back };
enum class UiAction { Draw, Previous, Next, DeleteMessage, DeleteAll };
struct PagerUi {
  Screen screen = Screen::Home;
  int selected = 0;
  int folder = 0;
  bool confirmYes = false;
  void showMessage() { screen = Screen::Inbox; selected = 0; confirmYes = false; }
  UiAction press(UiKey key) {
    if (key == UiKey::Back) { screen = Screen::Home; selected = 0; confirmYes = false; return UiAction::Draw; }
    switch (screen) {
      case Screen::Home:
        if (key == UiKey::Up) folder = (folder + 2) % 3;
        if (key == UiKey::Down) folder = (folder + 1) % 3;
        if (key == UiKey::Enter) screen = Screen::Inbox;
        break;
      case Screen::Inbox:
        if (key == UiKey::Up) return UiAction::Previous;
        if (key == UiKey::Down) return UiAction::Next;
        if (key == UiKey::Enter) { screen = Screen::Actions; selected = 0; }
        break;
      case Screen::Actions:
        if (key == UiKey::Up) selected = (selected + 3) % 4;
        if (key == UiKey::Down) selected = (selected + 1) % 4;
        if (key == UiKey::Enter) {
          if (selected == 0) screen = Screen::Home;
          else if (selected == 3) screen = Screen::Inbox;
          else { screen = selected == 1 ? Screen::ConfirmMessage : Screen::ConfirmAll; confirmYes = false; }
        }
        break;
      case Screen::ConfirmMessage:
      case Screen::ConfirmAll:
        if (key == UiKey::Up || key == UiKey::Down) confirmYes = !confirmYes;
        if (key == UiKey::Enter) {
          const bool all = screen == Screen::ConfirmAll;
          screen = Screen::Inbox;
          if (confirmYes) { confirmYes = false; return all ? UiAction::DeleteAll : UiAction::DeleteMessage; }
        }
        break;
    }
    return UiAction::Draw;
  }
};
