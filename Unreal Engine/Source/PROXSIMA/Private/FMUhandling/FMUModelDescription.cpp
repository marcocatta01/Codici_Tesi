#include "FMUModelDescription.h"
#include "XmlFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UFMUModelDescription::UFMUModelDescription()
    : DefaultStepSize(0.001)
{
}

bool UFMUModelDescription::InitializeFromExtractedFMU(const FString &extractedFMUPath)
{
    // The modelDescription.xml should be at the root of the extracted FMU
    FString xmlPath = FPaths::Combine(extractedFMUPath, TEXT("modelDescription.xml"));
    return ParseModelDescriptionXML(xmlPath);
}

bool UFMUModelDescription::ParseModelDescriptionXML(const FString &xmlPath)
{
    // Create temporary containers for parsed data
    TMap<FString, FFMUVariable> tempVariables;
    FString tempGUID;
    FString tempName;
    FString tempIdentifier;
    double tempStepSize = DefaultStepSize;

    FXmlFile xmlFile(xmlPath);
    if (!xmlFile.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse modelDescription.xml at: %s"), *xmlPath);
        return false;
    }

    const FXmlNode *rootNode = xmlFile.GetRootNode();
    if (!rootNode || rootNode->GetTag() != TEXT("fmiModelDescription"))
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid model description XML: missing fmiModelDescription root"));
        return false;
    }

    // Parse basic model information
    tempGUID = rootNode->GetAttribute(TEXT("guid"));
    tempName = rootNode->GetAttribute(TEXT("modelName"));

    // Find CoSimulation element and get modelIdentifier
    const FXmlNode *coSimNode = rootNode->FindChildNode(TEXT("CoSimulation"));
    if (coSimNode)
    {
        tempIdentifier = coSimNode->GetAttribute(TEXT("modelIdentifier"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FMU does not support Co-Simulation"));
        return false;
    }

    // Parse DefaultExperiment if present
    const FXmlNode *defaultExpNode = rootNode->FindChildNode(TEXT("DefaultExperiment"));
    if (defaultExpNode)
    {
        FString stepSizeStr = defaultExpNode->GetAttribute(TEXT("stepSize"));
        if (!stepSizeStr.IsEmpty())
        {
            tempStepSize = FCString::Atof(*stepSizeStr);
        }
    }

    // Parse ModelVariables
    const FXmlNode *modelVarsNode = rootNode->FindChildNode(TEXT("ModelVariables"));
    if (!modelVarsNode)
    {
        UE_LOG(LogTemp, Error, TEXT("No ModelVariables section found in XML"));
        return false;
    }

    bool hasValidVariables = false;
    for (const FXmlNode *varNode : modelVarsNode->GetChildrenNodes())
    {
        if (varNode->GetTag() == TEXT("ScalarVariable"))
        {
            FFMUVariable var;
            if (ParseScalarVariable(varNode, var) && var.IsValid())
            {
                hasValidVariables = true;
                tempVariables.Add(var.name, var);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Skipping invalid variable: %s"), *var.name);
            }
        }
    }

    if (!hasValidVariables)
    {
        UE_LOG(LogTemp, Warning, TEXT("No valid variables found in modelDescription.xml"));
        return false;
    }

    // Only update member variables after successful parsing
    FScopeLock Lock(&VariablesCriticalSection);
    ModelGUID = MoveTemp(tempGUID);
    ModelName = MoveTemp(tempName);
    ModelIdentifier = MoveTemp(tempIdentifier);
    DefaultStepSize = tempStepSize;
    Variables = MoveTemp(tempVariables);

    return true;
}

bool UFMUModelDescription::ParseScalarVariable(const FXmlNode *varNode, FFMUVariable &outVar)
{
    if (!varNode)
        return false;

    // Parse basic attributes
    outVar.name = varNode->GetAttribute(TEXT("name"));
    outVar.valueReference = FCString::Atoi(*varNode->GetAttribute(TEXT("valueReference")));
    outVar.description = varNode->GetAttribute(TEXT("description"));
    outVar.causality = varNode->GetAttribute(TEXT("causality"));
    outVar.variability = varNode->GetAttribute(TEXT("variability"));
    outVar.initial = varNode->GetAttribute(TEXT("initial"));

    // Find the type node (Real, Integer, Boolean, String)
    for (const FXmlNode *typeNode : varNode->GetChildrenNodes())
    {
        FString tag = typeNode->GetTag();
        outVar.type = tag;

        if (tag == TEXT("Real"))
        {
            // Parse Real-specific attributes
            FString startStr = typeNode->GetAttribute(TEXT("start"));
            FString minStr = typeNode->GetAttribute(TEXT("min"));
            FString maxStr = typeNode->GetAttribute(TEXT("max"));
            outVar.unit = typeNode->GetAttribute(TEXT("unit"));

            if (!startStr.IsEmpty())
                outVar.start = FCString::Atof(*startStr);
            if (!minStr.IsEmpty())
                outVar.min = FCString::Atof(*minStr);
            if (!maxStr.IsEmpty())
                outVar.max = FCString::Atof(*maxStr);
        }
        // We currently only support Real variables, but could add support for other types here
        break; // Only one type node should be present
    }

    return !outVar.name.IsEmpty();
}

bool UFMUModelDescription::ValidateValue(const FString &varName, double value) const
{
    // Thread-safe access to Variables map
    FScopeLock Lock(&VariablesCriticalSection);

    const FFMUVariable *var = Variables.Find(varName);
    if (!var)
    {
        UE_LOG(LogTemp, Warning, TEXT("Variable '%s' not found in model description"), *varName);
        return false;
    }

    // Only validate Real variables
    if (var->type != TEXT("Real"))
    {
        UE_LOG(LogTemp, Warning, TEXT("Variable '%s' is not a Real type"), *varName);
        return false;
    }

    // Check bounds if they exist
    if (var->min.IsSet() && value < var->min.GetValue())
    {
        FString message = FString::Printf(TEXT("Value %.6f is below minimum %.6f for variable '%s'"),
                                          value, var->min.GetValue(), *varName);
        UE_LOG(LogTemp, Warning, TEXT("%s"), *message);
        return false;
    }

    if (var->max.IsSet() && value > var->max.GetValue())
    {
        FString message = FString::Printf(TEXT("Value %.6f is above maximum %.6f for variable '%s'"),
                                          value, var->max.GetValue(), *varName);
        UE_LOG(LogTemp, Warning, TEXT("%s"), *message);
        return false;
    }

    return true;
}

bool UFMUModelDescription::GetValueReference(const FString &varName, uint32 &outValueRef) const
{
    // Thread-safe access to Variables map
    FScopeLock Lock(&VariablesCriticalSection);

    const FFMUVariable *var = Variables.Find(varName);
    if (!var)
    {
        UE_LOG(LogTemp, Error, TEXT("Variable '%s' not found in model description"), *varName);
        return false;
    }

    if (!var->IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Variable '%s' exists but is invalid (missing name or reference)"), *varName);
        return false;
    }

    outValueRef = var->valueReference;
    return true;
}