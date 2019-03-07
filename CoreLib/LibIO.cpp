#include "LibIO.h"
#include "Exception.h"
#ifndef __STDC__
#define __STDC__ 1
#endif
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
namespace CoreLib
{
	namespace IO
	{
		using namespace CoreLib::Basic;

		CommandLineWriter * currentCommandWriter = nullptr;

		void SetCommandLineWriter(CommandLineWriter * writer)
		{
			currentCommandWriter = writer;
		}

		bool File::Exists(const String & fileName)
		{
#ifdef _WIN32
			struct _stat32 statVar;
			return ::_wstat32(((String)fileName).ToWString(), &statVar) != -1;
#else
			struct stat statVar;
			return ::stat(fileName.Buffer(), &statVar) == 0;
#endif
		}

		String Path::TruncateExt(const String & path)
		{
			int dotPos = path.LastIndexOf('.');
			if (dotPos != -1)
				return path.SubString(0, dotPos);
			else
				return path;
		}
		String Path::ReplaceExt(const String & path, const char * newExt)
		{
			StringBuilder sb(path.Length()+10);
			int dotPos = path.LastIndexOf('.');
			if (dotPos == -1)
				dotPos = path.Length();
			sb.Append(path.Buffer(), dotPos);
			sb.Append('.');
			sb.Append(newExt);
			return sb.ProduceString();
		}
		String Path::GetFileName(const String & path)
		{
			int pos = path.LastIndexOf('/');
			pos = Math::Max(path.LastIndexOf('\\'), pos) + 1;
			return path.SubString(pos, path.Length()-pos);
		}
		String Path::GetFileNameWithoutEXT(const String & path)
		{
			int pos = path.LastIndexOf('/');
			pos = Math::Max(path.LastIndexOf('\\'), pos) + 1;
			int dotPos = path.LastIndexOf('.');
			if (dotPos <= pos)
				dotPos = path.Length();
			return path.SubString(pos, dotPos - pos);
		}
		String Path::GetFileExt(const String & path)
		{
			int dotPos = path.LastIndexOf('.');
			if (dotPos != -1)
				return path.SubString(dotPos+1, path.Length()-dotPos-1);
			else
				return "";
		}
		String Path::GetDirectoryName(const String & path)
		{
			int pos = path.LastIndexOf('/');
			pos = Math::Max(path.LastIndexOf('\\'), pos);
			if (pos != -1)
				return path.SubString(0, pos);
			else
				return "";
		}
		String Path::Combine(const String & path1, const String & path2)
		{
			if (path1.Length() == 0) return path2;
			StringBuilder sb(path1.Length()+path2.Length()+2);
			sb.Append(path1);
			if (!path1.EndsWith('\\') && !path1.EndsWith('/'))
				sb.Append(PathDelimiter);
			sb.Append(path2);
			return sb.ProduceString();
		}
		String Path::Combine(const String & path1, const String & path2, const String & path3)
		{
			StringBuilder sb(path1.Length()+path2.Length()+path3.Length()+3);
			sb.Append(path1);
			if (!path1.EndsWith('\\') && !path1.EndsWith('/'))
				sb.Append(PathDelimiter);
			sb.Append(path2);
			if (!path2.EndsWith('\\') && !path2.EndsWith('/'))
				sb.Append(PathDelimiter);
			sb.Append(path3);
			return sb.ProduceString();
		}

		bool Path::CreateDir(const String & path)
		{
#if defined(_WIN32)
			return _wmkdir(path.ToWString()) == 0;
#else 
			return mkdir(path.Buffer(), 0777) == 0;
#endif
		}

		List<String> Path::Split(String path)
		{
			List<String> dirs;
			StringBuilder sb;
			for (auto ch : path)
			{
				if (ch == Path::PathDelimiter || ch == Path::AltPathDelimiter)
				{
					auto d = sb.ToString();
					if (d.Length())
						dirs.Add(d);
					sb.Clear();
				}
				else
					sb << ch;
			}
			auto lastDir = sb.ToString();
			if (lastDir.Length())
				dirs.Add(lastDir);
			return dirs;
			
		}

		String Path::Normalize(String path)
		{
			List<String> dirs = Split(path);
			StringBuilder sb;
			for (int i = 0; i < dirs.Count(); i++)
			{
				sb << dirs[i];
				if (i != dirs.Count() - 1)
					sb << Path::PathDelimiter;
			}
			return sb.ProduceString();
		}

		bool Path::IsSubPathOf(String path, String parentPath)
		{
			if (parentPath.Length() < path.Length())
			{
#ifdef WIN32
				return path.ToLower().StartsWith(parentPath.ToLower());
#else
				return path.StartsWith(parentPath);
#endif
			}
			return false;
		}
		bool IsPathStringEqual(String p0, String p1)
		{
#ifdef WIN32
			if (p0.Length() != p1.Length())
				return false;
			for (int i = 0; i < p0.Length(); i++)
			{
				if (p0[i] != p1[i])
				{
					if (p0[i] >= 'A' && p0[i] <= 'Z')
					{
						if (p0[i] - 'A' + 'a' != p1[i])
							return false;
					}
					else if (p0[i] >= 'a' && p0[i] <= 'z')
					{
						if (p0[i] - 'a' + 'A' != p1[i])
							return false;
					}
					else
						return false;
				}
			}
			return true;
#else
			return p0 == p1;
#endif
		}
		String Path::GetRelativePath(String path, String referencePath)
		{
			auto dir1 = Split(path);
			auto dir2 = Split(referencePath);
			if (dir1.Count() > 0 && dir2.Count() > 0)
			{
				if (IsPathStringEqual(dir1[0], dir2[0]))
				{
					StringBuilder sb;
					int i = 1;
					while (i < Math::Min(dir1.Count(), dir2.Count()))
					{
						if (IsPathStringEqual(dir1[i], dir2[i]))
							i++;
						else
							break;
					}
					if (i < dir2.Count())
					{
						for (int j = i; j < dir2.Count(); j++)
							sb << ".." << PathDelimiter;
					}
					for (int j = i; j < dir1.Count(); j++)
					{
						sb << dir1[j];
						if (j != dir1.Count() - 1)
							sb << PathDelimiter;
					}
					return sb.ProduceString();
				}
			}
			return path;
		}

		CoreLib::Basic::String File::ReadAllText(const CoreLib::Basic::String & fileName)
		{
			StreamReader reader(new FileStream(fileName, FileMode::Open, FileAccess::Read, FileShare::ReadWrite));
			return reader.ReadToEnd();
		}

		CoreLib::Basic::List<unsigned char> File::ReadAllBytes(const CoreLib::Basic::String & fileName)
		{
			RefPtr<FileStream> fs = new FileStream(fileName, FileMode::Open, FileAccess::Read, FileShare::ReadWrite);
			List<unsigned char> buffer;
			while (!fs->IsEnd())
			{
				unsigned char ch;
				int read = (int)fs->Read(&ch, 1);
				if (read)
					buffer.Add(ch);
				else
					break;
			}
			return _Move(buffer);
		}

        void File::WriteAllBytes(const CoreLib::Basic::String & fileName, void * data, size_t size)
        {
            FileStream fs = FileStream(fileName, FileMode::Create);
            fs.Write(data, (Int64)size);
        }

		void File::WriteAllText(const CoreLib::Basic::String & fileName, const CoreLib::Basic::String & text)
		{
			StreamWriter writer(new FileStream(fileName, FileMode::Create));
			writer.Write(text);
		}
	}
}