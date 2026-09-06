#include "../Arduino Sketch/src/pager_text.h"
#include "../Arduino Sketch/src/button_debounce.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  char news[] = "F#BVHTCVSH!Sbvdifouxjdlmvoh";
  auto h = decodeSkyper(4520, news, strlen(news));
  assert(h.valid && h.headerLength == 2 && h.rubric == 39 && h.item == 3);
  assert(strncmp(news, "F#", 2) == 0);
  assert(strcmp(news + h.headerLength, "AUGSBURG Rauchentwicklung") == 0);
  char label[] = "1F*tbufmmjufo";
  h = decodeSkyper(4512, label, strlen(label));
  assert(h.valid && h.headerLength == 3 && h.rubric == 39);
  assert(strcmp(label + h.headerLength, "satelliten") == 0);
  for (uint32_t ric : {632967U, 1080U, 216U, 224U}) {
    char plain[] = "F#Plain! text 123";
    assert(!decodeSkyper(ric, plain, strlen(plain)).valid);
    assert(strcmp(plain, "F#Plain! text 123") == 0);
  }
  char shortText[] = "F#";
  assert(!decodeSkyper(4520, shortText, 2).valid);
  assert(!decodeSkyper(4520, nullptr, 100).valid);
  char malformed[] = "F#Hello world";
  assert(!decodeSkyper(4520, malformed, strlen(malformed)).valid);
  assert(strcmp(malformed, "F#Hello world") == 0);
  char badLabel[] = "2F*Ifmmp";
  assert(!decodeSkyper(4512, badLabel, strlen(badLabel)).valid);
  char padded[] = "F#Ifmmp\x03\x04";
  assert(decodeSkyper(4520, padded, strlen(padded)).valid);
  assert(strcmp(padded + 2, "Hello  ") == 0);
  char special[] = ";!WFSPO;!Qspqbhbujfojfvxt!\x14!xffl!48!3137";
  h = decodeSkyper(4520, special, strlen(special));
  assert(h.valid && h.rubric == 28 && h.item == 1);
  assert(strcmp(special + 2, "VERON: Propagatienieuws   week 37 2026") == 0);
  assert(strcmp(pagerGermanGlyph('{', true), "ä") == 0);
  assert(strcmp(pagerGermanGlyph('\\', false), "Oe") == 0);
  assert(strcmp(pagerGermanGlyph('~', false), "ss") == 0);
  assert(pagerGermanGlyph('A', true) == nullptr);
  char invalid[] = "F!bad\x80";
  assert(!decodeSkyper(4520, invalid, strlen(invalid)).valid);
  ButtonDebounce b;
  assert(!b.update(false, 100));
  assert(!b.update(true, 105));
  assert(!b.update(false, 110));
  assert(!b.update(false, 139));
  assert(b.update(false, 140));
  assert(!b.update(false, 1000)); // held button must not repeat
  assert(!b.update(true, 1001));
  assert(!b.update(true, 1031));
  assert(!b.update(false, 1040));
  assert(b.update(false, 1070));
  ButtonDebounce wrap;
  assert(!wrap.update(false, UINT32_MAX - 10));
  assert(!wrap.update(false, 18));
  assert(wrap.update(false, 19));
  std::cout << "PASS: Skyper news/labels, plain text, malformed input, padding, debounce and timer wrap\n";
}
