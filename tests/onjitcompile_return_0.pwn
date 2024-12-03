// OUTPUT: OK
// OUTPUT: \[jit\] Compilation was disabled
// OUTPUT: OK

#include <jit>
#include "test"

forward OnJITCompile();

main() {
	printf("%s", IsJITPresent() ? ("FAIL") : ("OK"));
}

public OnJITCompile() {
	printf("%s", IsJITPresent() ? ("FAIL") : ("OK"));
	return 0;
}
