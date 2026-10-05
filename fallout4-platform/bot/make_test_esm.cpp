// Writes a minimal synthetic Fallout4.esm (header + the Commonwealth
// worldspace) so a fallout4 server can start in tests without game data.
#include "PluginBuilder.h"

#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
  if (argc != 2) {
    std::cerr << "usage: fmp_make_test_esm <out/Fallout4.esm>\n";
    return 2;
  }
  test_espm::PluginBuilder b;
  b.SetFlags(1); // ESM
  b.AddRecord("WRLD", 0x3c).EditorId("Commonwealth");
  // The player base every new character is created from
  b.AddRecord("RACE", 0x13746).EditorId("HumanRace");
  test_espm::FieldWriter rnam;
  rnam.Add<uint32_t>(0x13746);
  b.AddRecord("NPC_", 0x7).EditorId("Player").Add("RNAM", rnam);
  auto bytes = b.Build();
  std::ofstream f(argv[1], std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return f ? 0 : 1;
}
