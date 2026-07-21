$ErrorActionPreference = 'Stop'

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$sourceTokenCache = @{}

function Remove-CLineSplices {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$SourceText)

    return [regex]::Replace($SourceText, '\\(?:\r\n|\n|\r)', '')
}

function Get-ActiveCTokens {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$SourceText)

    $source = Remove-CLineSplices -SourceText $SourceText
    $tokens = [System.Collections.Generic.List[object]]::new()
    $index = 0

    while ($index -lt $source.Length) {
        $character = $source[$index]
        $hasNext = ($index + 1) -lt $source.Length
        $nextCharacter = if ($hasNext) { $source[$index + 1] } else { [char]0 }

        if ([char]::IsWhiteSpace($character)) {
            $index++
            continue
        }

        if ($character -eq [char]47 -and $nextCharacter -eq [char]47) {
            $index += 2
            while ($index -lt $source.Length -and $source[$index] -notin @([char]10, [char]13)) {
                $index++
            }
            continue
        }

        if ($character -eq [char]47 -and $nextCharacter -eq [char]42) {
            $index += 2
            $foundBlockCommentEnd = $false
            while ($index + 1 -lt $source.Length) {
                if ($source[$index] -eq [char]42 -and $source[$index + 1] -eq [char]47) {
                    $index += 2
                    $foundBlockCommentEnd = $true
                    break
                }
                $index++
            }
            if (-not $foundBlockCommentEnd) {
                $index = $source.Length
            }
            continue
        }

        if ([char]::IsLetter($character) -or $character -eq [char]95) {
            $startIndex = $index
            $index++
            while ($index -lt $source.Length) {
                $candidate = $source[$index]
                if (-not ([char]::IsLetterOrDigit($candidate) -or $candidate -eq [char]95)) {
                    break
                }
                $index++
            }
            $tokens.Add([PSCustomObject]@{
                Kind = 'Identifier'
                Text = $source.Substring($startIndex, $index - $startIndex)
            })
            continue
        }

        if ([char]::IsDigit($character)) {
            $startIndex = $index
            $index++
            while ($index -lt $source.Length) {
                $candidate = $source[$index]
                if (-not ([char]::IsLetterOrDigit($candidate) -or $candidate -in @([char]46, [char]95))) {
                    break
                }
                $index++
            }
            $tokens.Add([PSCustomObject]@{
                Kind = 'Number'
                Text = $source.Substring($startIndex, $index - $startIndex)
            })
            continue
        }

        if ($character -in @([char]34, [char]39)) {
            $quote = $character
            $kind = if ($quote -eq [char]34) { 'StringLiteral' } else { 'CharacterLiteral' }
            $literal = [System.Text.StringBuilder]::new()
            [void]$literal.Append($character)
            $index++

            while ($index -lt $source.Length) {
                $character = $source[$index]
                [void]$literal.Append($character)
                $index++

                if ($character -eq [char]92 -and $index -lt $source.Length) {
                    [void]$literal.Append($source[$index])
                    $index++
                    continue
                }
                if ($character -eq $quote) {
                    break
                }
            }

            $tokens.Add([PSCustomObject]@{
                Kind = $kind
                Text = $literal.ToString()
            })
            continue
        }

        $tokens.Add([PSCustomObject]@{
            Kind = 'Punctuator'
            Text = [string]$character
        })
        $index++
    }

    return $tokens
}

function Get-SourceTokens {
    param([Parameter(Mandatory)][string]$RelativePath)

    if (-not $sourceTokenCache.ContainsKey($RelativePath)) {
        $sourcePath = Join-Path $workspaceRoot $RelativePath
        $sourceText = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
        $sourceTokenCache[$RelativePath] = @(Get-ActiveCTokens -SourceText $sourceText)
    }

    return $sourceTokenCache[$RelativePath]
}

function Test-CIdentifierPresent {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Tokens,
        [Parameter(Mandatory)][string]$Identifier
    )

    foreach ($token in $Tokens) {
        if ($token.Kind -eq 'Identifier' -and $token.Text -ceq $Identifier) {
            return $true
        }
    }
    return $false
}

function Test-CFunctionCall {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Tokens,
        [Parameter(Mandatory)][string]$FunctionName
    )

    for ($index = 0; $index -lt $Tokens.Count; $index++) {
        $token = $Tokens[$index]
        if ($token.Kind -ne 'Identifier' -or $token.Text -cne $FunctionName) {
            continue
        }

        $cursor = $index + 1
        $closingParenthesisCount = 0
        while ($cursor -lt $Tokens.Count -and $Tokens[$cursor].Text -eq ')') {
            $closingParenthesisCount++
            $cursor++
        }

        if ($cursor -ge $Tokens.Count -or $Tokens[$cursor].Text -ne '(') {
            continue
        }
        if ($closingParenthesisCount -eq 0) {
            return $true
        }
        if ($index - $closingParenthesisCount -lt 0) {
            continue
        }

        $hasMatchingWrapper = $true
        for ($offset = 1; $offset -le $closingParenthesisCount; $offset++) {
            if ($Tokens[$index - $offset].Text -ne '(') {
                $hasMatchingWrapper = $false
                break
            }
        }
        if ($hasMatchingWrapper) {
            return $true
        }
    }

    return $false
}

function Test-CTokenSequence {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Tokens,
        [Parameter(Mandatory)][object[]]$PatternTokens
    )

    if ($PatternTokens.Count -eq 0) {
        throw 'A contract token sequence cannot be empty.'
    }
    if ($Tokens.Count -lt $PatternTokens.Count) {
        return $false
    }

    for ($startIndex = 0; $startIndex -le $Tokens.Count - $PatternTokens.Count; $startIndex++) {
        $matchesSequence = $true
        for ($patternIndex = 0; $patternIndex -lt $PatternTokens.Count; $patternIndex++) {
            $sourceToken = $Tokens[$startIndex + $patternIndex]
            $patternToken = $PatternTokens[$patternIndex]
            if ($sourceToken.Kind -cne $patternToken.Kind -or $sourceToken.Text -cne $patternToken.Text) {
                $matchesSequence = $false
                break
            }
        }
        if ($matchesSequence) {
            return $true
        }
    }

    return $false
}

function Assert-ContractState {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$Description,
        [Parameter(Mandatory)][bool]$IsPresent,
        [Parameter(Mandatory)][bool]$ShouldBePresent
    )

    if ($ShouldBePresent -and -not $IsPresent) {
        throw "Missing required active code in ${RelativePath}: $Description"
    }
    if (-not $ShouldBePresent -and $IsPresent) {
        throw "Unexpected active code in ${RelativePath}: $Description"
    }
}

function Assert-CFunctionCallState {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$FunctionName,
        [Parameter(Mandatory)][bool]$ShouldBePresent
    )

    $tokens = @(Get-SourceTokens -RelativePath $RelativePath)
    $isPresent = Test-CFunctionCall -Tokens $tokens -FunctionName $FunctionName
    Assert-ContractState -RelativePath $RelativePath -Description "call to $FunctionName" -IsPresent $isPresent -ShouldBePresent $ShouldBePresent
}

function Assert-CIdentifierState {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$Identifier,
        [Parameter(Mandatory)][bool]$ShouldBePresent
    )

    $tokens = @(Get-SourceTokens -RelativePath $RelativePath)
    $isPresent = Test-CIdentifierPresent -Tokens $tokens -Identifier $Identifier
    Assert-ContractState -RelativePath $RelativePath -Description "identifier $Identifier" -IsPresent $isPresent -ShouldBePresent $ShouldBePresent
}

function Assert-CTokenSequenceState {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$CodeText,
        [Parameter(Mandatory)][bool]$ShouldBePresent
    )

    $tokens = @(Get-SourceTokens -RelativePath $RelativePath)
    $patternTokens = @(Get-ActiveCTokens -SourceText $CodeText)
    $isPresent = Test-CTokenSequence -Tokens $tokens -PatternTokens $patternTokens
    Assert-ContractState -RelativePath $RelativePath -Description $CodeText -IsPresent $isPresent -ShouldBePresent $ShouldBePresent
}

# Self-probes exercise the same tokenizer and matchers used by the contract tables.
if (Test-CFunctionCall -Tokens @(Get-ActiveCTokens -SourceText 'const char *s = "RobotCMDInit();";') -FunctionName 'RobotCMDInit') {
    throw 'Self-test failed: a function name inside a string literal was treated as an active call.'
}
if (Test-CFunctionCall -Tokens @(Get-ActiveCTokens -SourceText 'DoNotShootInit();') -FunctionName 'ShootInit') {
    throw 'Self-test failed: an identifier substring was treated as the disabled function.'
}
if (-not (Test-CFunctionCall -Tokens @(Get-ActiveCTokens -SourceText '(ShootInit)();') -FunctionName 'ShootInit')) {
    throw 'Self-test failed: a parenthesized disabled function call was not detected.'
}
$continuedLineCommentProbe = '// comment \' + "`r`n" + 'RobotCMDInit();'
if (Test-CFunctionCall -Tokens @(Get-ActiveCTokens -SourceText $continuedLineCommentProbe) -FunctionName 'RobotCMDInit') {
    throw 'Self-test failed: a line-spliced comment exposed a required function call.'
}

$commentedRequiredProbe = @'
/* RobotCMDInit(); */
// GimbalInit();
'@
$commentedRequiredTokens = @(Get-ActiveCTokens -SourceText $commentedRequiredProbe)
if (Test-CFunctionCall -Tokens $commentedRequiredTokens -FunctionName 'RobotCMDInit') {
    throw 'Self-test failed: a block-commented required call was treated as active code.'
}
if (Test-CFunctionCall -Tokens $commentedRequiredTokens -FunctionName 'GimbalInit') {
    throw 'Self-test failed: a line-commented required call was treated as active code.'
}
if (-not (Test-CFunctionCall -Tokens @(Get-ActiveCTokens -SourceText 'ShootInit ();') -FunctionName 'ShootInit')) {
    throw 'Self-test failed: whitespace variation bypassed disabled-call detection.'
}
if (-not (Test-CIdentifierPresent -Tokens @(Get-ActiveCTokens -SourceText '/* ignored */x') -Identifier 'x')) {
    throw 'Self-test failed: code immediately following a block comment was skipped.'
}
Write-Output 'C tokenizer self-probes passed.'

$disabledCalls = @(
    [PSCustomObject]@{ Path = 'application/robot.c'; Call = 'ShootInit' }
    [PSCustomObject]@{ Path = 'application/robot.c'; Call = 'ShootTask' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Call = 'CANCommInit' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Call = 'CANCommGet' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Call = 'CANCommSend' }
)

foreach ($entry in $disabledCalls) {
    Assert-CFunctionCallState -RelativePath $entry.Path -FunctionName $entry.Call -ShouldBePresent $false
}

$disabledIdentifiers = @(
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Identifier = 'CANCommInstance' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Identifier = 'CANComm_Init_Config_s' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Identifier = 'cmd_can_comm' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Identifier = 'shoot_cmd_pub' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Identifier = 'shoot_feed_sub' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Identifier = 'shoot_fetch_data' }
)

foreach ($entry in $disabledIdentifiers) {
    Assert-CIdentifierState -RelativePath $entry.Path -Identifier $entry.Identifier -ShouldBePresent $false
}

$disabledTokenSequences = @(
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Code = '#include "can_comm.h"' }
)

foreach ($entry in $disabledTokenSequences) {
    Assert-CTokenSequenceState -RelativePath $entry.Path -CodeText $entry.Code -ShouldBePresent $false
}

$requiredCalls = @(
    [PSCustomObject]@{ Path = 'application/robot.c'; Call = 'RobotCMDInit' }
    [PSCustomObject]@{ Path = 'application/robot.c'; Call = 'GimbalInit' }
    [PSCustomObject]@{ Path = 'application/robot.c'; Call = 'RobotCMDTask' }
    [PSCustomObject]@{ Path = 'application/robot.c'; Call = 'GimbalTask' }
)

foreach ($entry in $requiredCalls) {
    Assert-CFunctionCallState -RelativePath $entry.Path -FunctionName $entry.Call -ShouldBePresent $true
}

$requiredTokenSequences = @(
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Code = 'RemoteControlInit(&huart3)' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Code = 'TransferImageInit(&huart6)' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Code = 'VisionInit(&huart1)' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Code = 'RemoteControlSet();' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Code = 'ImageMouseKeySet();' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Code = 'gimbal_cmd_send.gimbal_mode = GIMBAL_ZERO_FORCE;' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Code = 'PubPushMessage(gimbal_cmd_pub, (void *)&gimbal_cmd_send);' }
)

foreach ($entry in $requiredTokenSequences) {
    Assert-CTokenSequenceState -RelativePath $entry.Path -CodeText $entry.Code -ShouldBePresent $true
}

Write-Output 'Gimbal/vision command-path contract passed.'
