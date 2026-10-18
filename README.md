# File Collector

Тестовое задание Intern C++ SafeBoard 2026
Реализация потокобезопасного класса `FileCollector`, который собирает файлы, передаваемые чанками в произвольном порядке.
**GitHub:** [github.com/Fedor2005/FileCollector](https://github.com/Fedor2005/FileCollector)

## Task

Нужно реализовать потокобезопасный класс `FileCollector`, который собирает файлы из чанков. Каждый чанк содержит данные и позицию в файле. Чанки приходят в произвольном порядке, могут пересекаться и повторяться, а один файл могут одновременно дописывать несколько потоков. Класс должен эффективно хранить полученные данные, правильно обрабатывать разрывы, пересечения и дубликаты и минимизировать блокировки и копирование. Интерфейс получения файла нужно было спроектировать самостоятельно: он должен позволять читать файл частично и целиком, без лишнего копирования и безопасно при конкурентном доступе. Решение покрыто тестами на GoogleTest, включая многопоточные, сборка через CMake или Bazel.

## Содержание


## Стек

- С++, LTS
- GTest, gcov

## Сборка
Linux:
CMake
```bash
mkdir build && cd build
cmake ..
make
```

Bazel
```bash
bazel build //...
```

Windows:
CMake
```powershell
mkdir build
cd build
cmake ..
cmake --build .
```
Bazel
```powershell
bazel build //...
```

## Тесты

### CMake
Для CMake рекомендуется использовать отдельную директорию `build`.

**Стандартная сборка и тесты:**
```bash
mkdir build && cd build
cmake ..
make
ctest
```

**Сборка с Thread Sanitizer (TSAN):**
```bash
mkdir build && cd build
cmake .. -DUSE_TSAN=ON
make
ctest
```

**Сборка с покрытием кода (Coverage):**
```bash
mkdir build && cd build
cmake .. -DENABLE_COVERAGE=ON
make
ctest
make coverage
```

### Bazel
Bazel работает из корня проекта.

**Стандартная сборка и тесты:**
```bash
bazel build //...
bazel test //...
```

**Сборка с TSAN:**
```bash
bazel test --config=tsan //...
```

**Сборка с покрытием (Coverage):**
```bash
bazel coverage //...
```

### Windows (MSVC)
```powershell
# CMake
mkdir build; cd build; cmake ..; cmake --build .
ctest

# Bazel
bazel build //...
bazel test //...
```
## Что реализовано
- Методы `CollectFile(fileId, fileSize)`, `OnNewChunk(fileId, pos, chunk)`
- 

## Что НЕ реализовано / ограничения

##  Решение (ПОЧЕМУ)

## Non overlapping intervals

Структура данных, реализующая логику интервалов
- Добавление нового интервала `add_interval(interval)`
- Поиск непокрытых интервалов `uncovered_intervals(interval)`
Шаблоный класс, требование для параметра шаблона - полный порядок (т.е. любые два экземпляра класса можно сравнить).
Является оберткой над контейнером std::map<T, T>.
Класс гарантирует, что интервалы не пересекаются `non overlapping`.



## Структура проекта

```text
 FileCollector
├──  CMakeLists.txt
├──  include
│   ├──  Exception.hpp
│   ├──  FileCollector.hpp
│   ├──  NonOverlappingIntervals.hpp
│   └──  NonOverlappingIntervals.tpp
├──  README.md
├── 󱧼 src
│   ├──  Exception.cpp
│   └──  FileCollector.cpp
├──  Task.md
└── 󰙨 test
    ├──  FileCollectorTest.cpp
    └──  NonOverlappingIntervalsTest.cpp

```


## Контакты

- **Имя:** Фёдор Михалев
- **Email:** [f.v.mikhalev@mail.ru](mailto:f.v.mikhalev@mail.ru)
- **Telegram:** [@mikhfv](https://t.me/mikhfv)
- **GitHub:** [github.com/Fedor2005](https://github.com/Fedor2005)

## О себе

