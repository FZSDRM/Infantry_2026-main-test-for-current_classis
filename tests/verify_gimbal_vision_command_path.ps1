$ErrorActionPreference = 'Stop'

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$sourceTokenCache = @{}
$functionBodyTokenCache = @{}

function Remove-CLineSplices {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$SourceText)

    return [regex]::Replace($SourceText, '\\(?:\r\n|\n|\r)', '')
}

function Get-ActiveCTokens {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$SourceText)

    $source = Remove-CLineSplices -SourceText $SourceText
    $tokens = [System.Collections.Generic.List[object]]::new()
    $index = 0
    $logicalLine = 1
    $atLogicalLineStart = $true
    $inPreprocessorDirective = $false

    while ($index -lt $source.Length) {
        $character = $source[$index]
        $hasNext = ($index + 1) -lt $source.Length
        $nextCharacter = if ($hasNext) { $source[$index + 1] } else { [char]0 }

        if ($character -eq [char]13) {
            $logicalLine++
            $atLogicalLineStart = $true
            $inPreprocessorDirective = $false
            if ($nextCharacter -eq [char]10) {
                $index += 2
            }
            else {
                $index++
            }
            continue
        }
        if ($character -eq [char]10) {
            $logicalLine++
            $atLogicalLineStart = $true
            $inPreprocessorDirective = $false
            $index++
            continue
        }
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
                if ($source[$index] -eq [char]13) {
                    $logicalLine++
                    $atLogicalLineStart = $true
                    $inPreprocessorDirective = $false
                    if ($source[$index + 1] -eq [char]10) {
                        $index++
                    }
                }
                elseif ($source[$index] -eq [char]10) {
                    $logicalLine++
                    $atLogicalLineStart = $true
                    $inPreprocessorDirective = $false
                }
                $index++
            }
            if (-not $foundBlockCommentEnd) {
                throw 'Unterminated C block comment.'
            }
            continue
        }

        if ($atLogicalLineStart -and $character -eq [char]35) {
            $inPreprocessorDirective = $true
        }
        $atLogicalLineStart = $false

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
                LogicalLine = $logicalLine
                IsPreprocessorDirective = $inPreprocessorDirective
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
                LogicalLine = $logicalLine
                IsPreprocessorDirective = $inPreprocessorDirective
            })
            continue
        }

        if ($character -in @([char]34, [char]39)) {
            $quote = $character
            $kind = if ($quote -eq [char]34) { 'StringLiteral' } else { 'CharacterLiteral' }
            $literal = [System.Text.StringBuilder]::new()
            $foundLiteralEnd = $false
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
                    $foundLiteralEnd = $true
                    break
                }
            }

            if (-not $foundLiteralEnd) {
                throw "Unterminated C $kind."
            }

            $tokens.Add([PSCustomObject]@{
                Kind = $kind
                Text = $literal.ToString()
                LogicalLine = $logicalLine
                IsPreprocessorDirective = $inPreprocessorDirective
            })
            continue
        }

        $tokens.Add([PSCustomObject]@{
            Kind = 'Punctuator'
            Text = [string]$character
            LogicalLine = $logicalLine
            IsPreprocessorDirective = $inPreprocessorDirective
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

function Get-CFunctionBodyTokens {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Tokens,
        [Parameter(Mandatory)][string]$FunctionName
    )

    $globalBraceDepth = 0

    for ($nameIndex = 0; $nameIndex -lt $Tokens.Count; $nameIndex++) {
        $nameToken = $Tokens[$nameIndex]

        if (-not $nameToken.IsPreprocessorDirective -and $nameToken.Text -eq '{') {
            $globalBraceDepth++
            continue
        }
        if (-not $nameToken.IsPreprocessorDirective -and $nameToken.Text -eq '}') {
            $globalBraceDepth--
            if ($globalBraceDepth -lt 0) {
                throw 'Unbalanced global C brace depth.'
            }
            continue
        }

        if ($globalBraceDepth -ne 0 -or $nameToken.IsPreprocessorDirective -or
            $nameToken.Kind -ne 'Identifier' -or $nameToken.Text -cne $FunctionName) {
            continue
        }

        $parameterStart = $nameIndex + 1
        if ($parameterStart -ge $Tokens.Count -or $Tokens[$parameterStart].IsPreprocessorDirective -or
            $Tokens[$parameterStart].Text -ne '(') {
            continue
        }

        $parenthesisDepth = 0
        $parameterEnd = -1
        for ($tokenIndex = $parameterStart; $tokenIndex -lt $Tokens.Count; $tokenIndex++) {
            if ($Tokens[$tokenIndex].IsPreprocessorDirective) {
                continue
            }
            if ($Tokens[$tokenIndex].Text -eq '(') {
                $parenthesisDepth++
            }
            elseif ($Tokens[$tokenIndex].Text -eq ')') {
                $parenthesisDepth--
                if ($parenthesisDepth -eq 0) {
                    $parameterEnd = $tokenIndex
                    break
                }
            }
        }

        if ($parameterEnd -lt 0) {
            throw "Unbalanced parameter list while locating C function: $FunctionName"
        }

        $bodyStart = $parameterEnd + 1
        while ($bodyStart -lt $Tokens.Count -and $Tokens[$bodyStart].IsPreprocessorDirective) {
            $bodyStart++
        }
        if ($bodyStart -ge $Tokens.Count -or $Tokens[$bodyStart].Text -ne '{') {
            continue
        }

        $braceDepth = 0
        $bodyTokens = [System.Collections.Generic.List[object]]::new()
        for ($tokenIndex = $bodyStart; $tokenIndex -lt $Tokens.Count; $tokenIndex++) {
            $token = $Tokens[$tokenIndex]
            if (-not $token.IsPreprocessorDirective -and $token.Text -eq '{') {
                $braceDepth++
                if ($braceDepth -gt 1) {
                    $bodyTokens.Add($token)
                }
                continue
            }
            if (-not $token.IsPreprocessorDirective -and $token.Text -eq '}') {
                $braceDepth--
                if ($braceDepth -eq 0) {
                    return $bodyTokens
                }
                if ($braceDepth -lt 0) {
                    throw "Unbalanced function body while locating C function: $FunctionName"
                }
                $bodyTokens.Add($token)
                continue
            }
            if ($braceDepth -gt 0) {
                $bodyTokens.Add($token)
            }
        }

        throw "Unterminated function body while locating C function: $FunctionName"
    }

    throw "C function definition not found: $FunctionName"
}

function Get-SourceFunctionBodyTokens {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$FunctionName
    )

    $cacheKey = "${RelativePath}::${FunctionName}"
    if (-not $functionBodyTokenCache.ContainsKey($cacheKey)) {
        $sourceTokens = @(Get-SourceTokens -RelativePath $RelativePath)
        $functionBodyTokenCache[$cacheKey] = @(Get-CFunctionBodyTokens -Tokens $sourceTokens -FunctionName $FunctionName)
    }

    return $functionBodyTokenCache[$cacheKey]
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

function Test-CBodyFunctionCall {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Tokens,
        [Parameter(Mandatory)][string]$FunctionName
    )

    $bodyTokens = @($Tokens | Where-Object { -not $_.IsPreprocessorDirective })

    for ($index = 0; $index -lt $bodyTokens.Count; $index++) {
        $token = $bodyTokens[$index]
        if ($token.Kind -ne 'Identifier' -or $token.Text -cne $FunctionName) {
            continue
        }

        $calleeStart = -1
        $argumentStart = -1

        if ($index + 2 -lt $bodyTokens.Count -and
            $bodyTokens[$index + 1].Text -eq ')' -and $bodyTokens[$index + 2].Text -eq '(') {
            if ($index -ge 2 -and $bodyTokens[$index - 1].Text -eq '*' -and $bodyTokens[$index - 2].Text -eq '(') {
                $calleeStart = $index - 2
                $argumentStart = $index + 2
            }
            elseif ($index -ge 1 -and $bodyTokens[$index - 1].Text -eq '(') {
                $calleeStart = $index - 1
                $argumentStart = $index + 2
            }
        }
        elseif ($index + 1 -lt $bodyTokens.Count -and $bodyTokens[$index + 1].Text -eq '(') {
            $calleeStart = $index
            $argumentStart = $index + 1
        }

        if ($calleeStart -lt 0) {
            continue
        }

        $parenthesisDepth = 0
        $argumentEnd = -1
        for ($cursor = $argumentStart; $cursor -lt $bodyTokens.Count; $cursor++) {
            if ($bodyTokens[$cursor].Text -eq '(') {
                $parenthesisDepth++
            }
            elseif ($bodyTokens[$cursor].Text -eq ')') {
                $parenthesisDepth--
                if ($parenthesisDepth -eq 0) {
                    $argumentEnd = $cursor
                    break
                }
            }
        }

        if ($argumentEnd -lt 0 -or $argumentEnd + 1 -ge $bodyTokens.Count -or
            $bodyTokens[$argumentEnd + 1].Text -ne ';') {
            continue
        }

        if ($calleeStart -eq 0) {
            return $true
        }

        $prefixToken = $bodyTokens[$calleeStart - 1]
        if ($prefixToken.Text -in @(';', '{', '}', ':', ')')) {
            return $true
        }
        if ($prefixToken.Kind -eq 'Identifier' -and $prefixToken.Text -in @('do', 'else')) {
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
            if ($sourceToken.Kind -cne $patternToken.Kind -or
                $sourceToken.Text -cne $patternToken.Text -or
                $sourceToken.IsPreprocessorDirective -ne $patternToken.IsPreprocessorDirective) {
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
        [Parameter(Mandatory)][string]$BodyFunction,
        [Parameter(Mandatory)][string]$FunctionName,
        [Parameter(Mandatory)][bool]$ShouldBePresent
    )

    $bodyTokens = @(Get-SourceFunctionBodyTokens -RelativePath $RelativePath -FunctionName $BodyFunction |
        Where-Object { -not $_.IsPreprocessorDirective })
    $isPresent = Test-CBodyFunctionCall -Tokens $bodyTokens -FunctionName $FunctionName
    Assert-ContractState -RelativePath $RelativePath -Description "call to $FunctionName in $BodyFunction" -IsPresent $isPresent -ShouldBePresent $ShouldBePresent
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

function Assert-CBodyTokenSequenceState {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$BodyFunction,
        [Parameter(Mandatory)][string]$CodeText,
        [Parameter(Mandatory)][bool]$ShouldBePresent
    )

    $bodyTokens = @(Get-SourceFunctionBodyTokens -RelativePath $RelativePath -FunctionName $BodyFunction |
        Where-Object { -not $_.IsPreprocessorDirective })
    $patternTokens = @(Get-ActiveCTokens -SourceText $CodeText)
    $isPresent = Test-CTokenSequence -Tokens $bodyTokens -PatternTokens $patternTokens
    Assert-ContractState -RelativePath $RelativePath -Description "$CodeText in $BodyFunction" -IsPresent $isPresent -ShouldBePresent $ShouldBePresent
}

# Self-probes exercise the same tokenizer and matchers used by the contract tables.
$functionContextProbe = @'
#define RequiredCall() replacement
void RequiredCall(void);
void RequiredCall(void) {}
void EmptyBody(void) {
    void RequiredCall(void);
    #define RequiredCall() replacement
}
void CallingBody(void) { RequiredCall(); }
'@
$functionContextTokens = @(Get-ActiveCTokens -SourceText $functionContextProbe)
$emptyBodyTokens = @(Get-CFunctionBodyTokens -Tokens $functionContextTokens -FunctionName 'EmptyBody')
if (Test-CBodyFunctionCall -Tokens $emptyBodyTokens -FunctionName 'RequiredCall') {
    throw 'Self-test failed: a macro, prototype, or definition was treated as a body call.'
}
$callingBodyTokens = @(Get-CFunctionBodyTokens -Tokens $functionContextTokens -FunctionName 'CallingBody')
if (-not (Test-CBodyFunctionCall -Tokens $callingBodyTokens -FunctionName 'RequiredCall')) {
    throw 'Self-test failed: an actual function-body call was not detected.'
}

$macroBodyProbeTokens = @(Get-ActiveCTokens -SourceText '#define TargetBody() { Required(); }')
$macroBodyRejected = $false
try {
    @(Get-CFunctionBodyTokens -Tokens $macroBodyProbeTokens -FunctionName 'TargetBody') | Out-Null
}
catch {
    if ($_.Exception.Message -like 'C function definition not found:*') {
        $macroBodyRejected = $true
    }
    else {
        throw
    }
}
if (-not $macroBodyRejected) {
    throw 'Self-test failed: a function-like macro was treated as a function definition.'
}

$typedefPrototypeTokens = @(Get-ActiveCTokens -SourceText 'void PrototypeBody(void) { Result_t RequiredCall(void); }')
$typedefPrototypeBody = @(Get-CFunctionBodyTokens -Tokens $typedefPrototypeTokens -FunctionName 'PrototypeBody')
if (Test-CBodyFunctionCall -Tokens $typedefPrototypeBody -FunctionName 'RequiredCall') {
    throw 'Self-test failed: a typedef-return prototype was treated as a body call.'
}
if (-not (Test-CBodyFunctionCall -Tokens @(Get-ActiveCTokens -SourceText '(*ShootInit)();') -FunctionName 'ShootInit')) {
    throw 'Self-test failed: a dereferenced disabled function call was not detected.'
}

$nonCallFragments = @(
    '#define RequiredCall() replacement'
    'void RequiredCall(void);'
    'void RequiredCall(void) {}'
)
foreach ($nonCallFragment in $nonCallFragments) {
    if (Test-CBodyFunctionCall -Tokens @(Get-ActiveCTokens -SourceText $nonCallFragment) -FunctionName 'RequiredCall') {
        throw "Self-test failed: a macro, prototype, or definition was treated as a call: $nonCallFragment"
    }
}

$unterminatedInputs = @(
    '/* unterminated block comment'
    '"unterminated string'
    "'unterminated character"
)
foreach ($unterminatedInput in $unterminatedInputs) {
    $lexerThrew = $false
    try {
        @(Get-ActiveCTokens -SourceText $unterminatedInput) | Out-Null
    }
    catch {
        $lexerThrew = $true
    }
    if (-not $lexerThrew) {
        throw "Self-test failed: lexer accepted unterminated input: $unterminatedInput"
    }
}

if (Test-CBodyFunctionCall -Tokens @(Get-ActiveCTokens -SourceText 'const char *s = "RobotCMDInit();";') -FunctionName 'RobotCMDInit') {
    throw 'Self-test failed: a function name inside a string literal was treated as an active call.'
}
if (Test-CBodyFunctionCall -Tokens @(Get-ActiveCTokens -SourceText 'DoNotShootInit();') -FunctionName 'ShootInit') {
    throw 'Self-test failed: an identifier substring was treated as the disabled function.'
}
if (-not (Test-CBodyFunctionCall -Tokens @(Get-ActiveCTokens -SourceText '(ShootInit)();') -FunctionName 'ShootInit')) {
    throw 'Self-test failed: a parenthesized disabled function call was not detected.'
}
$continuedLineCommentProbe = '// comment \' + "`r`n" + 'RobotCMDInit();'
if (Test-CBodyFunctionCall -Tokens @(Get-ActiveCTokens -SourceText $continuedLineCommentProbe) -FunctionName 'RobotCMDInit') {
    throw 'Self-test failed: a line-spliced comment exposed a required function call.'
}

$commentedRequiredProbe = @'
/* RobotCMDInit(); */
// GimbalInit();
'@
$commentedRequiredTokens = @(Get-ActiveCTokens -SourceText $commentedRequiredProbe)
if (Test-CBodyFunctionCall -Tokens $commentedRequiredTokens -FunctionName 'RobotCMDInit') {
    throw 'Self-test failed: a block-commented required call was treated as active code.'
}
if (Test-CBodyFunctionCall -Tokens $commentedRequiredTokens -FunctionName 'GimbalInit') {
    throw 'Self-test failed: a line-commented required call was treated as active code.'
}
if (-not (Test-CBodyFunctionCall -Tokens @(Get-ActiveCTokens -SourceText 'ShootInit ();') -FunctionName 'ShootInit')) {
    throw 'Self-test failed: whitespace variation bypassed disabled-call detection.'
}
if (-not (Test-CIdentifierPresent -Tokens @(Get-ActiveCTokens -SourceText '/* ignored */x') -Identifier 'x')) {
    throw 'Self-test failed: code immediately following a block comment was skipped.'
}
Write-Output 'C tokenizer self-probes passed.'

$disabledCalls = @(
    [PSCustomObject]@{ Path = 'application/robot.c'; Body = 'RobotInit'; Call = 'ShootInit' }
    [PSCustomObject]@{ Path = 'application/robot.c'; Body = 'RobotTask'; Call = 'ShootTask' }
)

foreach ($entry in $disabledCalls) {
    Assert-CFunctionCallState -RelativePath $entry.Path -BodyFunction $entry.Body -FunctionName $entry.Call -ShouldBePresent $false
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

$disabledBodyTokenSequences = @(
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDInit'; Code = 'shoot_cmd_pub = PubRegister("shoot_cmd", sizeof(Shoot_Ctrl_Cmd_s));' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDInit'; Code = 'shoot_feed_sub = SubRegister("shoot_feed", sizeof(Shoot_Upload_Data_s));' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDInit'; Code = 'cmd_can_comm = CANCommInit(&comm_conf);' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDTask'; Code = 'chassis_fetch_data = *(Chassis_Upload_Data_s *)CANCommGet(cmd_can_comm);' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDTask'; Code = 'SubGetMessage(shoot_feed_sub, &shoot_fetch_data);' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDTask'; Code = 'CANCommSend(cmd_can_comm, (void *)&chassis_cmd_send);' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDTask'; Code = 'PubPushMessage(shoot_cmd_pub, (void *)&shoot_cmd_send);' }
)

foreach ($entry in $disabledBodyTokenSequences) {
    Assert-CBodyTokenSequenceState -RelativePath $entry.Path -BodyFunction $entry.Body -CodeText $entry.Code -ShouldBePresent $false
}

$requiredCalls = @(
    [PSCustomObject]@{ Path = 'application/robot.c'; Body = 'RobotInit'; Call = 'RobotCMDInit' }
    [PSCustomObject]@{ Path = 'application/robot.c'; Body = 'RobotInit'; Call = 'GimbalInit' }
    [PSCustomObject]@{ Path = 'application/robot.c'; Body = 'RobotTask'; Call = 'RobotCMDTask' }
    [PSCustomObject]@{ Path = 'application/robot.c'; Body = 'RobotTask'; Call = 'GimbalTask' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDTask'; Call = 'RemoteControlSet' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDTask'; Call = 'ImageMouseKeySet' }
)

foreach ($entry in $requiredCalls) {
    Assert-CFunctionCallState -RelativePath $entry.Path -BodyFunction $entry.Body -FunctionName $entry.Call -ShouldBePresent $true
}

$requiredTokenSequences = @(
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDInit'; Code = 'RemoteControlInit(&huart3)' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDInit'; Code = 'TransferImageInit(&huart6)' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDInit'; Code = 'VisionInit(&huart1)' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDTask'; Code = 'gimbal_cmd_send.gimbal_mode = GIMBAL_ZERO_FORCE;' }
    [PSCustomObject]@{ Path = 'application/cmd/robot_cmd.c'; Body = 'RobotCMDTask'; Code = 'PubPushMessage(gimbal_cmd_pub, (void *)&gimbal_cmd_send);' }
)

foreach ($entry in $requiredTokenSequences) {
    Assert-CBodyTokenSequenceState -RelativePath $entry.Path -BodyFunction $entry.Body -CodeText $entry.Code -ShouldBePresent $true
}

Write-Output 'Gimbal/vision command-path contract passed.'
