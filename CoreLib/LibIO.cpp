#include "LibIO.h"
#include "Exception.h"
#ifndef __STDC__
#define __STDC__ 1
#endif

#if __has_include(<filesystem>)
#define CPP17_FILESYSTEM 1
#include <filesystem>
namespace filesystem = std::filesystem;
#else
// Non-C++17: use posix headers
#include <sys/stat.h>
#ifdef __linux__
#include <dirent.h>
#endif
#ifdef _WIN32
#include <direct.h>
#endif
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
#if defined(CPP17_FILESYSTEM)
			return filesystem::exists(filesystem::u8path(fileName.Buffer()));
#elif defined(_WIN32)
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
			{
				if (path.Length() == 1) // root
					return path;
				return path.SubString(0, pos);
			}
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
#if defined(CPP17_FILESYSTEM)
			return filesystem::create_directory(filesystem::u8path(path.Buffer()));
#elif defined(_WIN32)
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
			if (path.StartsWith("\\\\"))
				sb << "\\\\";
			else if (path.StartsWith(Path::PathDelimiter))
				sb << Path::PathDelimiter;
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

		bool Path::IsDirectory(CoreLib::String path)
		{
			if (path.EndsWith(Path::PathDelimiter) || path.EndsWith(Path::AltPathDelimiter))
				return true;
#if defined(CPP17_FILESYSTEM)
			return filesystem::is_directory(filesystem::u8path(path.Buffer()));
#elif defined(__linux__)
			struct stat s;
			if (stat(path.Buffer(), &s) == 0)
			{
				return (s.st_mode & S_IFDIR);
			}
			return false;
#elif defined(_WIN32)
			struct _stat s;
			if (_wstat(path.ToWString(), &s) == 0)
			{
				return (s.st_mode & _S_IFDIR);
			}
			return false;
#endif
		}

		bool Path::IsAbsolute(CoreLib::String path)
		{
#if defined(CPP17_FILESYSTEM)
			return filesystem::u8path(path.Buffer()).is_absolute();
#elif defined(__linux__)
			return path.StartsWith(Path::PathDelimiter);
#else
			return path.IndexOf(':') != -1 || path.StartsWith("\\\\");
#endif
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

		DirectoryIterator::~DirectoryIterator()
		{
		}

		DirectoryIterator::DirectoryIterator(const DirectoryIterator &other)
			: context(other.context)
		{
		}

		DirectoryIterator::DirectoryIterator(DirectoryIterator &&other)
			: context(_Move(other.context))
		{
		}

#ifdef CPP17_FILESYSTEM
		struct DirectoryIteratorContext : public RefObject
		{
			filesystem::directory_iterator iter;
			~DirectoryIteratorContext()
			{
			}
		};

		DirectoryIterator::DirectoryIterator()
		{
			context = new DirectoryIteratorContext();
		}

		DirectoryIterator::DirectoryIterator(CoreLib::String path)
		{
			context = new DirectoryIteratorContext();
			context->iter = filesystem::directory_iterator(filesystem::u8path(path.Buffer()));
		}

		DirectoryEntry DirectoryIterator::operator*()
		{
			auto& entry = *context->iter;
			DirectoryEntry result;
			if (filesystem::is_directory(entry.path()))
				result.type = DirectoryEntryType::Directory;
			else if (filesystem::is_regular_file(entry.path()))
				result.type = DirectoryEntryType::File;
			else
				result.type = DirectoryEntryType::Unknown;
			auto u8str = entry.path().u8string();
			result.fullPath = CoreLib::String(u8str.data(), (int)u8str.length());
			result.name = Path::GetFileName(result.fullPath);
			return result;
		}
		DirectoryIterator &DirectoryIterator::operator++()
		{
			++context->iter;
			return *this;
		}

		bool DirectoryIterator::operator==(const DirectoryIterator &other)
		{
			return context->iter == other.context->iter;
		}
#else
		struct DirectoryIteratorContext : public RefObject
		{
			DIR *dir;
			struct dirent* ent;
			CoreLib::String dirPath;
			~DirectoryIteratorContext()
			{
				closedir(dir);
			}
		};

		DirectoryIterator::DirectoryIterator()
		{
		}

		DirectoryIterator::DirectoryIterator(CoreLib::String path)
		{
			DIR* dir = opendir(path.Buffer());
			if (dir)
			{
				context = new DirectoryIteratorContext();
				context->dir = dir;
				context->ent = readdir(dir);
				context->dirPath = path;
			}
		}

		DirectoryEntry DirectoryIterator::operator*()
		{
			CORELIB_ASSERT(context->ent);
			DirectoryEntry ent;
			ent.name = context->ent->d_name;
			ent.fullPath = Path::Combine(context->dirPath, ent.name);
			if (context->ent->d_type == DT_DIR)
				ent.type = DirectoryEntryType::Directory;
			else if (context->ent->d_type == DT_REG)
				ent.type = DirectoryEntryType::File;
			else
				ent.type = DirectoryEntryType::Unknown;
			if (ent.name == "." || ent.name == "..")
				ent.type = DirectoryEntryType::Unknown;
			return ent;
		}

		DirectoryIterator& DirectoryIterator::operator++()
		{
			context->ent = readdir(context->dir);
			return *this;
		}

		bool DirectoryIterator::operator==(const DirectoryIterator &other)
		{
			if (!context)
			{
				return !other.context || other.context->ent == nullptr;
			}
			else
			{
				if (other.context)
				{
					return context->ent == other.context->ent;
				}
				else
				{
					return context->ent == nullptr;
				}
			}
		}
#endif
	}
}