# hop

키 하나(기본값 `XF86Tools`)로 작업공간을 전환하는 작은 X11 데몬입니다.

| 입력 | 동작 |
|---|---|
| `Tools` 짧게 누르기 | 다음 작업공간으로 이동 (마지막 다음은 처음) |
| `Tools` 누른 채 `1`–`9`, `0` | 1–10번 작업공간으로 이동 |

`Tools`를 누르고 있는 동안에는 키보드 입력을 hop이 받으므로, 함께 누른 숫자가 포커스된 창에 입력되지 않습니다. 전환은 EWMH `_NET_CURRENT_DESKTOP` 요청으로 창 관리자에 맡기므로 EWMH를 지원하는 창 관리자라면 동작합니다(xfwm4에서 확인).

## 요구 사항

- X11(`libX11` 헤더), C 컴파일러, `pkg-config`
- 작업공간이 여러 개인 창 관리자. xfwm4라면 다음과 같이 늘릴 수 있습니다.

  ```sh
  xfconf-query -c xfwm4 -p /general/workspace_count -s 10
  ```

## 빌드와 설치

```sh
make
make install      # ~/.local/bin/hop 과 XFCE 자동 실행 항목을 설치
```

다시 로그인하지 않고 바로 실행하려면:

```sh
setsid -f ~/.local/bin/hop
```

제거는 `make uninstall`로 합니다.
