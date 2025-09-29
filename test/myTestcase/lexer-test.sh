#!/bin/bash

for src in testfile*.txt; do
    num=$(echo "$src" | grep -oE '[0-9]+')

    exe="test${num}"
    input="input${num}.txt"
    output="output${num}.txt"
    code="testfile${num}.c"

    if [ -f "$output" ]; then
      rm "$output"
    fi
    cp "$src" "$code"

    # compile
    gcc "$code" -o "$exe"
    if [ $? -ne 0 ]; then
        echo "[$src] 编译失败"
        rm "$code"
        continue
    fi

    # run
    ./"$exe" < "$input" > "$output"
    if [ $? -ne 0 ]; then
        echo "[$src] 运行失败"
        continue
    fi
    echo "[$src] 运行成功，输出保存在 $output"

    rm "$exe" "$code"
done


if [ "$1" == "release" ]; then
    for src in testfile*.txt; do
      # 删除前9行
      cp "$src" "$src.bak"
      sed -i '' '1,9d' "$src"

      num=$(echo "$src" | grep -oE '[0-9]+')
      input="input${num}.txt"
      output="output${num}.txt"
      zip "lexer-test.zip" "$src" "$input" "$output"

      rm "$src"
      mv "$src.bak" "$src"
    done
    exit 0
fi
