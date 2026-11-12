package org.zowe.zo.ffm;

import java.lang.foreign.Linker;
import java.lang.foreign.SymbolLookup;
import java.nio.file.Paths;

public class NativeLoader {
    private static final SymbolLookup LOOKUP;
    public static final Linker LINKER = Linker.nativeLinker();

    static {
        try {
            System.load(Paths.get("../bindings/build-out/libzo_java.so").toAbsolutePath().toString());
        } catch (UnsatisfiedLinkError e) {
            System.err.println("Failed to load native library from ../bindings/build-out/libzo_java.so");
            System.err.println("Trying to load from java.library.path instead...");
            System.loadLibrary("zo_java");
        }
        LOOKUP = SymbolLookup.loaderLookup();
    }

    public static SymbolLookup getLookup() {
        return LOOKUP;
    }
}
