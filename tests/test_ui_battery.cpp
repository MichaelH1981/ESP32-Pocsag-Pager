#include "../Arduino Sketch/src/pager_ui.h"
#include "../Arduino Sketch/src/battery_filter.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
 PagerUi u;
 assert(u.screen==Screen::Home);
 u.press(UiKey::Enter); assert(u.screen==Screen::Inbox);
 assert(u.press(UiKey::Up)==UiAction::Previous);
 u.press(UiKey::Enter);assert(u.screen==Screen::Actions && u.selected==0);
 u.press(UiKey::Enter);assert(u.screen==Screen::Home);
 u.press(UiKey::Enter);u.press(UiKey::Enter);u.press(UiKey::Down);u.press(UiKey::Enter);
 assert(u.screen==Screen::ConfirmMessage && !u.confirmYes);
 assert(u.press(UiKey::Enter)==UiAction::Draw); // default No
 u.press(UiKey::Enter);u.press(UiKey::Down);u.press(UiKey::Enter);u.press(UiKey::Down);
 assert(u.press(UiKey::Enter)==UiAction::DeleteMessage);
 u.press(UiKey::Enter);u.press(UiKey::Down);u.press(UiKey::Down);u.press(UiKey::Enter);
 assert(u.screen==Screen::ConfirmAll);
 u.press(UiKey::Down);u.showMessage(); // incoming message cancels stale confirmation
 assert(u.screen==Screen::Inbox && !u.confirmYes);
 for(Screen screen : {Screen::Home,Screen::Inbox,Screen::Actions,Screen::ConfirmMessage,Screen::ConfirmAll}) {
  u.screen=screen;u.press(UiKey::Back);assert(u.screen==Screen::Home);
 }
 u.folder=0;
 u.press(UiKey::Down);assert(u.folder==1 && u.screen==Screen::Home);
 u.press(UiKey::Down);assert(u.folder==2);
 u.press(UiKey::Down);assert(u.folder==0);
 u.press(UiKey::Up);assert(u.folder==2);
 u.press(UiKey::Enter);assert(u.screen==Screen::Inbox && u.folder==2);
 u.press(UiKey::Back);assert(u.screen==Screen::Home && u.folder==2);
 KeyTracker key;
 assert(key.update(false,0)==KeyEvent::None);
 assert(key.update(false,30)==KeyEvent::None);
 assert(key.update(true,100)==KeyEvent::None);
 assert(key.update(true,130)==KeyEvent::Short);
 assert(key.update(false,200)==KeyEvent::None);
 assert(key.update(false,230)==KeyEvent::None);
 assert(key.update(false,1029)==KeyEvent::None);
 assert(key.update(false,1030)==KeyEvent::Long);
 assert(key.update(false,2030)==KeyEvent::None);
 assert(key.update(true,2040)==KeyEvent::None);
 assert(key.update(true,2070)==KeyEvent::None); // no short event after long press
 uint32_t samples[16];for(auto& s:samples)s=2050;
 samples[0]=0;samples[1]=3300;
 float voltage=0;
 assert(batteryVolts(samples,2,1,0,voltage) && std::fabs(voltage-4.1f)<0.001f);
 assert(batteryVolts(samples,2,1.01f,-0.01f,voltage) && std::fabs(voltage-4.131f)<0.001f);
 for(auto& s:samples)s=0;
 assert(!batteryVolts(samples,2,1,0,voltage));
 for(auto& s:samples)s=3300;
 assert(!batteryVolts(samples,2,1,0,voltage));
 std::cout<<"PASS: navigation, home escape, delete confirmation, incoming-message cancellation, short/long keys, battery filtering/calibration\n";
}
