// NxDiag.cs -- какие классы-скрипты редактор видит в DLL из Assets/Plugins:
// fileID каждого MonoScript, чтобы сверить со ссылками m_Script в сценах.
//   Unity.exe -batchmode -quit -projectPath ukport -executeMethod NxDiag.ListScripts
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

public static class NxDiag
{
    public static void ListScripts()
    {
        var sb = new StringBuilder();
        foreach (var path in Directory.GetFiles("Assets/Plugins", "*.dll"))
        {
            var p = path.Replace('\\', '/');
            var objs = AssetDatabase.LoadAllAssetsAtPath(p);
            int n = 0;
            foreach (var o in objs)
            {
                if (!(o is MonoScript ms)) continue;
                if (AssetDatabase.TryGetGUIDAndLocalFileIdentifier(o, out string guid, out long fid))
                {
                    var cls = ms.GetClass();
                    sb.AppendLine($"{guid}\t{fid}\t{Path.GetFileName(p)}\t{(cls != null ? cls.FullName : "<null:" + ms.name + ">")}");
                    n++;
                }
            }
            Debug.Log($"[nxdiag] {p}: {n} scripts");
        }
        File.WriteAllText("scripts.tsv", sb.ToString());
        Debug.Log("[nxdiag] written scripts.tsv");
    }
}
