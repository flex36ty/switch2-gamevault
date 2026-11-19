#include "console/include/companion_paths.hpp"
#include <cassert>
int main(){
 assert(vaultImportFile("p0/package.bin"));
 assert(vaultImportFile("p1/0123456789abcdef0123456789abcdef.cnmt.nca"));
 assert(vaultImportFile("save-0123456789abcdef/files/slot.dat"));
 assert(!vaultImportFile("p0/restore-current.txt"));
 assert(!vaultImportFile("p0/restore-0123456789abcdef/journal-0.bin"));
 assert(!vaultImportFile("p0/evil.nro"));
}
