--TEST--
Exceptions on improper usage of $this
--FILE--
<?php
abstract class C {
	abstract static function foo();
}

function foo(callable $x) {
}

try {
	C::foo();
} catch (EngineException $e) {
	echo "\nException: " . $e->getMessage() . " in " , $e->getFile() . " on line " . $e->getLine() . "\n";
}

try {
	foo("C::foo");
} catch (EngineException $e) {
	echo "\n";
	do {
		echo "Exception: " . $e->getMessage() . "\n";
		$e = $e->getPrevious();
	} while ($e instanceof EngineException);
}

C::foo();
?>
--EXPECTF--
Exception: Cannot call abstract method C::foo() in %sexception_017.php on line %d

Exception: Argument 1 passed to foo() must be callable, string given, called in %sexception_017.php on line %d
Exception: Cannot call abstract method C::foo()

Fatal error: Uncaught exception 'EngineException' with message 'Cannot call abstract method C::foo()' in %sexception_017.php:%d
Stack trace:
#0 {main}
  thrown in %sexception_017.php on line %d
