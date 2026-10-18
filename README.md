# File Collector

Тестовое задание Intern C++ SafeBoard 2026
Реализация потокобезопасного класса `FileCollector`, который собирает файлы, передаваемые чанками в произвольном порядке.
**GitHub:** [github.com/Fedor2005/FileCollector](https://github.com/Fedor2005/FileCollector)

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
```

Windows:
CMake
```PowerShell
```
Bazel
```PowerShell
```

## Тесты

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

