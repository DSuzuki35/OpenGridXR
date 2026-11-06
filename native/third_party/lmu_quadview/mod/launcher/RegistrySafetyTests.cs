using System;
using System.Reflection;
using System.Runtime.InteropServices;

class RegistrySafetyTests {
    // Never execute the regression's forbidden call: it crashes this machine's
    // kernel. Inspect the compiled imports before running any registry tests.
    static int Main(string[] args) {
        var assembly=args.Length==1?Assembly.LoadFrom(args[0]):Assembly.GetExecutingAssembly();
        if(args.Length==1 && !Attribute.IsDefined(assembly.EntryPoint,typeof(STAThreadAttribute))) {
            Console.Error.WriteLine("Launcher entry point must use STA for Windows dialogs and COM controls");
            return 1;
        }
        foreach(var type in assembly.GetTypes())
            foreach(var method in type.GetMethods(BindingFlags.Static|BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.DeclaredOnly)) {
                var import=(DllImportAttribute)Attribute.GetCustomAttribute(method,typeof(DllImportAttribute));
                if(import!=null && (import.EntryPoint.IndexOf("RenameKey",StringComparison.OrdinalIgnoreCase)>=0)) {
                    Console.Error.WriteLine("Unsafe registry rename import: "+type.FullName+"."+method.Name);
                    return 1;
                }
            }
        Console.WriteLine("Registry safety guard passed: no registry-key rename imports");
        return 0;
    }
}
